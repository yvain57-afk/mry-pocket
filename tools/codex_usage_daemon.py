#!/usr/bin/env python3
"""
Codex usage daemon — scans ~/.codex/sessions/ for the most recent
rate_limits record and serves it over HTTP.

Codex's JSONL session files contain `event_msg` lines of type
`token_count` whose payload includes `rate_limits.primary` (5-hour
window) and `rate_limits.secondary` (weekly), with `used_percent`,
`window_minutes`, and `resets_at` (epoch seconds).

Usage:
    python3 codex_usage_daemon.py --port 8888 --host 0.0.0.0
    python3 codex_usage_daemon.py --scan   # dry run, print latest data

Endpoints:
    GET /codex/usage  → JSON with primary + secondary + plan + meta
    GET /health       → "ok"
"""

from __future__ import annotations
import argparse
import json
import os
import socket
import sys
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

CODEX_SESSIONS = Path.home() / ".codex" / "sessions"

# ── Cache so we don't re-scan disk on every HTTP request ──
_cache: dict = {"value": None, "fetched_at": 0.0}
_CACHE_TTL = 5.0   # seconds


def find_recent_jsonl_files(limit: int = 8) -> list[Path]:
    """Return up to `limit` most-recently-modified .jsonl files under sessions/."""
    if not CODEX_SESSIONS.exists():
        return []
    files = list(CODEX_SESSIONS.rglob("*.jsonl"))
    files.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return files[:limit]


def last_token_count_in(path: Path) -> dict | None:
    """Walk a JSONL backwards-ish (read all, take last matching) for token_count."""
    last = None
    try:
        with path.open("r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                # Cheap filter before JSON parse
                if "token_count" not in line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                payload = obj.get("payload", {})
                if payload.get("type") == "token_count":
                    last = (obj.get("timestamp"), payload)
    except OSError:
        return None
    return last


def fetch_latest_rate_limits() -> dict | None:
    """Find the newest token_count event across recent session files."""
    best = None
    for f in find_recent_jsonl_files():
        result = last_token_count_in(f)
        if result is None:
            continue
        ts, payload = result
        if best is None or (ts and ts > best[0]):
            best = (ts, payload, f)

    if best is None:
        return None

    ts, payload, source = best
    rl = payload.get("rate_limits") or {}
    info = payload.get("info") or {}
    return {
        "primary":   rl.get("primary"),
        "secondary": rl.get("secondary"),
        "plan_type": rl.get("plan_type"),
        "model_context_window": info.get("model_context_window"),
        "last_token_usage": info.get("last_token_usage"),
        "total_token_usage": info.get("total_token_usage"),
        "source_file": str(source.relative_to(CODEX_SESSIONS)),
        "source_timestamp": ts,
    }


def _annotate_window_state(data: dict, now_epoch: float) -> dict:
    """
    Codex writes `token_count` only when a turn completes, so the
    embedded rate_limits become a frozen snapshot. We don't fudge the
    number (that would lie when the user used Codex Desktop/web in the
    meantime); we just annotate whether the recorded window has since
    rolled over, and advance resets_at to the next true wall-clock
    reset so countdown UIs make sense.
    """
    if not data:
        return data

    for slot in ("primary", "secondary"):
        rl = data.get(slot)
        if not rl:
            continue
        resets_at = rl.get("resets_at")
        window_min = rl.get("window_minutes")
        if not resets_at or not window_min:
            continue
        window_s = window_min * 60
        rolled = now_epoch >= resets_at
        if rolled:
            # Roll resets_at forward until it's in the future
            new_resets = resets_at
            while new_resets <= now_epoch:
                new_resets += window_s
            rl["resets_at"] = new_resets
        rl["window_already_reset"] = rolled
    return data


def cached_payload() -> dict:
    now = time.time()
    if _cache["value"] is None or (now - _cache["fetched_at"]) > _CACHE_TTL:
        raw = fetch_latest_rate_limits()
        _cache["value"] = _annotate_window_state(raw, now)
        _cache["fetched_at"] = now

    data = _cache["value"]
    # Surface staleness so the client can warn user when no turns have
    # happened recently — even after rollover, that means the figure is
    # a guess (0%) not a measurement.
    source_ts = data.get("source_timestamp") if data else None
    stale_seconds = None
    if source_ts:
        try:
            src_dt = datetime.fromisoformat(source_ts.replace("Z", "+00:00"))
            stale_seconds = int(now - src_dt.timestamp())
        except (ValueError, AttributeError):
            pass

    return {
        "device_name":   socket.gethostname(),
        "fetched_at":    datetime.now(timezone.utc).isoformat(),
        "stale_seconds": stale_seconds,
        "data":          data,
    }


# ─────────────────── HTTP layer ───────────────────

class Handler(BaseHTTPRequestHandler):
    def _send_json(self, code: int, body: dict) -> None:
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):  # noqa: N802
        if self.path == "/health":
            self._send_json(200, {"status": "ok"})
        elif self.path in ("/codex/usage", "/usage"):
            self._send_json(200, cached_payload())
        else:
            self._send_json(404, {"error": "not found", "path": self.path})

    # quieter logging
    def log_message(self, fmt, *args):
        sys.stderr.write("[%s] %s\n" % (self.log_date_time_string(), fmt % args))


def serve(host: str, port: int) -> None:
    server = ThreadingHTTPServer((host, port), Handler)
    print(f"codex-usage-daemon listening on http://{host}:{port}", flush=True)
    print(f"  /codex/usage   latest rate_limits", flush=True)
    print(f"  /health        liveness", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


def scan() -> None:
    payload = cached_payload()
    print(json.dumps(payload, indent=2, ensure_ascii=False))


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="0.0.0.0")
    p.add_argument("--port", type=int, default=8888)
    p.add_argument("--scan", action="store_true",
                   help="Print latest data and exit (dry run)")
    args = p.parse_args()

    if args.scan:
        scan()
        return
    serve(args.host, args.port)


if __name__ == "__main__":
    main()
