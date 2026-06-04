// mode_monitor.h — MONITOR group: Codex 5-hour usage ring + countdown.
//
// One sub-mode per configured endpoint (Mac mini, Air, ...). On the
// active page, fetch /codex/usage every POLL_INTERVAL_MS and render
// `primary.used_percent` as a ring, with reset countdown below.
#pragma once
#include "app.h"
#include "wifi_config.h"

class MonitorMode : public Mode {
public:
    void enter() override;
    void exit() override;
    void tick(uint32_t now_ms) override;
    void onEvent(BtnEvent e) override;
    void cycleSubMode() override;
    const char* name() const override { return "MON"; }
    const char* subName() const override;
    void renderPanel(M5Canvas& c, int x, int y, int w, int h) override;

private:
    // Per-endpoint cached fetch result
    struct PageState {
        bool     have_data         = false;
        float    primary_pct       = 0.0f;   // last observed, may be stale
        float    secondary_pct     = 0.0f;
        uint32_t primary_resets    = 0;       // epoch, advanced past now by daemon
        uint32_t secondary_resets  = 0;
        bool     primary_rolled    = false;   // window has cycled since record
        bool     secondary_rolled  = false;
        uint32_t stale_seconds     = 0;       // age of underlying record
        uint32_t last_fetch_ms     = 0;
        bool     last_fetch_ok     = false;
        char     err_msg[32]       = "";
    };

    static constexpr uint32_t STALE_THRESHOLD_S = 30 * 60;   // 30 min

    uint8_t   sub_ = 0;
    PageState pages_[WiFiConfig::ENDPOINT_COUNT];

    // WiFi state
    enum WifiState : uint8_t { WF_BOOT, WF_CONNECTING, WF_READY, WF_FAILED };
    WifiState wifi_state_ = WF_BOOT;
    uint32_t  wifi_attempt_started_ms_ = 0;

    void startWifiIfNeeded();
    void doFetch(uint8_t idx, uint32_t now_ms);
};
