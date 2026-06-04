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
        bool     have_data       = false;
        float    primary_pct     = 0.0f;
        float    secondary_pct   = 0.0f;
        uint32_t resets_at_epoch = 0;
        uint32_t last_fetch_ms   = 0;     // millis() of last attempt
        bool     last_fetch_ok   = false;
        char     err_msg[32]     = "";
    };

    uint8_t   sub_ = 0;
    PageState pages_[WiFiConfig::ENDPOINT_COUNT];

    // WiFi state
    enum WifiState : uint8_t { WF_BOOT, WF_CONNECTING, WF_READY, WF_FAILED };
    WifiState wifi_state_ = WF_BOOT;
    uint32_t  wifi_attempt_started_ms_ = 0;

    void startWifiIfNeeded();
    void doFetch(uint8_t idx, uint32_t now_ms);
    void renderRing(M5Canvas& c, int cx, int cy, int r,
                    float pct, uint16_t fg_col) const;
};
