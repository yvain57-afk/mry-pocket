#!/usr/bin/env bash
# Install codex-usage-daemon as a per-user LaunchAgent on this Mac.
#
# Idempotent: re-running rebinds to the current absolute script path
# (so you can pull updates and re-run safely).
#
# Run on Mac mini AND on the MBP Air.

set -euo pipefail

cd "$(dirname "$0")"
ROOT="$(pwd)"
SRC_SCRIPT="${ROOT}/codex_usage_daemon.py"
PLIST_TEMPLATE="${ROOT}/com.mry.codex-usage-daemon.plist"

LABEL="com.mry.codex-usage-daemon"
PLIST_DEST="${HOME}/Library/LaunchAgents/${LABEL}.plist"

# Daemon script must live under $HOME so launchd can always read it
# (external volumes like /Volumes/Macboy often deny launchd access).
INSTALL_DIR="${HOME}/.local/share/mry-pocket"
SCRIPT="${INSTALL_DIR}/codex_usage_daemon.py"

if [[ ! -f "$SRC_SCRIPT" ]]; then
    echo "❌ Missing $SRC_SCRIPT" >&2
    exit 1
fi

mkdir -p "${HOME}/Library/LaunchAgents" "${HOME}/Library/Logs" "$INSTALL_DIR"

# Copy script to home — re-running this updates the deployed copy
cp "$SRC_SCRIPT" "$SCRIPT"
chmod +x "$SCRIPT"
echo "→ Deployed daemon to $SCRIPT"

# Render plist with absolute paths substituted
sed \
    -e "s|__SCRIPT_PATH__|${SCRIPT}|g" \
    -e "s|__HOME__|${HOME}|g" \
    "$PLIST_TEMPLATE" > "$PLIST_DEST"

# Unload previous instance if present, then load fresh
launchctl unload "$PLIST_DEST" 2>/dev/null || true
launchctl load -w "$PLIST_DEST"

# Give it a beat then verify
sleep 1
if curl -fsS "http://127.0.0.1:8888/health" >/dev/null 2>&1; then
    echo "✅ codex-usage-daemon running."
    echo "   Hostname:   $(scutil --get LocalHostName).local"
    echo "   URL:        http://$(scutil --get LocalHostName).local:8888/codex/usage"
    echo "   Logs:       ~/Library/Logs/codex-usage-daemon.{out,err}"
    echo
    curl -s "http://127.0.0.1:8888/codex/usage" | python3 -m json.tool | head -30
else
    echo "❌ Daemon didn't respond on :8888. Check logs:"
    echo "   tail ~/Library/Logs/codex-usage-daemon.err"
    exit 1
fi
