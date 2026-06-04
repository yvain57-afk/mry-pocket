// wifi_config.h — single source of truth for WiFi credentials and the
// list of Codex-usage endpoints the MONITOR group polls.
//
// Edit and reflash to change. Future work: persist to NVS so the user
// can configure on-device.
#pragma once
#include <Arduino.h>

namespace WiFiConfig {

// ── Network ──
static constexpr const char* SSID     = "ChinaNet-mkbh";
static constexpr const char* PASSWORD = "3rdmhmvb";

// ── Codex usage endpoints ──
// Each MONITOR sub-mode page polls one of these. mDNS hostnames work on
// macOS by default; if discovery flakes, replace with raw IP.
struct Endpoint {
    const char* label;   // shown on screen
    const char* host;    // hostname or IP, no scheme, no port
    uint16_t    port;
    const char* path;
};

static const Endpoint ENDPOINTS[] = {
    { "MAC MINI", "yvain-Macmini-2.local", 8888, "/codex/usage" },
    { "AIR",      "yvain-MBA.local",       8888, "/codex/usage" },  // adjust hostname after Air install
};
static constexpr uint8_t ENDPOINT_COUNT = sizeof(ENDPOINTS) / sizeof(ENDPOINTS[0]);

// Poll interval (ms) — daemon caches 5s server-side so 5s here is enough
static constexpr uint32_t POLL_INTERVAL_MS = 5000;
// WiFi connect timeout (ms) per attempt
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;

}  // namespace WiFiConfig
