#pragma once
#include "app.h"

class UtilityMode : public Mode {
public:
    void enter() override;
    void exit() override;
    void tick(uint32_t now_ms) override;
    void onEvent(BtnEvent e) override;
    void cycleSubMode() override;
    const char* name() const override { return "UTIL"; }
    const char* subName() const override;
    void renderPanel(M5Canvas& c, int x, int y, int w, int h) override;

private:
    enum Sub : uint8_t { SUB_BRIGHT = 0, SUB_TVBGONE = 1, SUB_COUNT };
    Sub sub_ = SUB_BRIGHT;
    uint8_t bright_step_ = 1;  // 0..4
    static constexpr uint8_t BRIGHT_LEVELS[5] = {20, 60, 120, 180, 240};

    // ── TV-B-Gone state machine ──
    bool     tvbg_running_ = false;
    uint16_t tvbg_idx_     = 0;
    uint32_t tvbg_last_ms_ = 0;
    uint16_t tvbg_total_   = 0;   // set in enter() from TV_CODES count
};
