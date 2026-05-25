#pragma once
#include "app.h"

class TimerMode : public Mode {
public:
    void enter() override;
    void exit() override;
    void tick(uint32_t now_ms) override;
    void onEvent(BtnEvent e) override;
    void cycleSubMode() override;
    const char* name() const override { return "TIMER"; }
    const char* subName() const override;
    void renderPanel(M5Canvas& c, int x, int y, int w, int h) override;

private:
    enum Sub : uint8_t { SUB_SW = 0, SUB_COUNTDOWN = 1, SUB_POMODORO = 2, SUB_COUNT };
    Sub sub_ = SUB_SW;

    bool     running_ = false;
    uint32_t start_ms_ = 0;
    uint32_t accum_ms_ = 0;

    // Countdown: durations in seconds
    static constexpr uint32_t CD_OPTIONS_S[3] = { 30, 60, 120 };
    uint8_t  cd_idx_ = 0;
    int32_t  cd_remaining_ms_ = 30 * 1000;
    uint8_t  cd_last_beep_sec_ = 99;

    // Pomodoro: work/break in minutes
    static constexpr uint8_t POM_OPTIONS[2][2] = { {25, 5}, {50, 10} };
    uint8_t  pom_idx_ = 0;
    bool     pom_in_break_ = false;
    int32_t  pom_remaining_ms_ = 25 * 60 * 1000;
};
