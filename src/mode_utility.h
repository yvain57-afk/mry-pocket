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
    enum Sub : uint8_t { SUB_BRIGHT = 0, SUB_VOLUME = 1, SUB_COUNT };
    Sub sub_ = SUB_BRIGHT;

    uint8_t bright_step_ = 1;   // 0..4
    static constexpr uint8_t BRIGHT_LEVELS[5] = { 20, 60, 120, 180, 240 };

    uint8_t volume_step_ = 3;   // 0..4
    static constexpr uint8_t VOLUME_LEVELS[5] = { 40, 100, 160, 210, 255 };
};
