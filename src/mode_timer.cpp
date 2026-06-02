#include "mode_timer.h"
#include "ui.h"
#include "audio.h"

constexpr uint32_t TimerMode::CD_OPTIONS_S[3];
constexpr uint8_t  TimerMode::POM_OPTIONS[2][2];

static const char* SUB_NAMES[] = { "STOPWATCH", "COUNTDOWN", "POMODORO" };

void TimerMode::enter() {
    // Reset to clean state when entering — but preserve config
    running_ = false;
}

void TimerMode::exit() {
    running_ = false;
}

const char* TimerMode::subName() const {
    return SUB_NAMES[sub_];
}

void TimerMode::cycleSubMode() {
    running_ = false;
    accum_ms_ = 0;
    sub_ = (Sub)((sub_ + 1) % SUB_COUNT);
    if (sub_ == SUB_COUNTDOWN) {
        cd_remaining_ms_ = CD_OPTIONS_S[cd_idx_] * 1000;
        cd_last_beep_sec_ = 99;
    } else if (sub_ == SUB_POMODORO) {
        pom_in_break_ = false;
        pom_remaining_ms_ = POM_OPTIONS[pom_idx_][0] * 60 * 1000;
    }
    Audio::beep(1400, 30);
}

void TimerMode::tick(uint32_t now_ms) {
    if (!running_) return;

    if (sub_ == SUB_SW) {
        // accum_ms_ + (now - start_ms_) is current elapsed
        // nothing else to do
    } else if (sub_ == SUB_COUNTDOWN) {
        int32_t elapsed_in_run = (int32_t)(now_ms - start_ms_);
        cd_remaining_ms_ -= elapsed_in_run;
        start_ms_ = now_ms;
        if (cd_remaining_ms_ <= 0) {
            cd_remaining_ms_ = 0;
            running_ = false;
            // final triple beep
            Audio::beep(2400, 150);
            delay(180);
            Audio::beep(2400, 150);
            delay(180);
            Audio::beep(3200, 350);
        } else {
            // beep on last 3 seconds
            uint8_t sec = (cd_remaining_ms_ + 999) / 1000;
            if (sec <= 3 && sec != cd_last_beep_sec_) {
                Audio::beep(1800, 60);
                cd_last_beep_sec_ = sec;
            }
        }
    } else if (sub_ == SUB_POMODORO) {
        int32_t elapsed_in_run = (int32_t)(now_ms - start_ms_);
        pom_remaining_ms_ -= elapsed_in_run;
        start_ms_ = now_ms;
        if (pom_remaining_ms_ <= 0) {
            // phase end — toggle work/break
            pom_in_break_ = !pom_in_break_;
            uint8_t mins = pom_in_break_ ? POM_OPTIONS[pom_idx_][1] : POM_OPTIONS[pom_idx_][0];
            pom_remaining_ms_ = (int32_t)mins * 60 * 1000;
            // distinctive chime
            if (pom_in_break_) {
                Audio::beepDouble(1800, 1200);  // descending — break starts
            } else {
                Audio::beepDouble(1200, 1800);  // ascending — work starts
            }
        }
    }
}

void TimerMode::onEvent(BtnEvent e) {
    if (e == EV_A_SHORT) {
        // primary toggle start/pause
        if (sub_ == SUB_SW) {
            if (running_) {
                accum_ms_ += millis() - start_ms_;
                running_ = false;
            } else {
                start_ms_ = millis();
                running_ = true;
            }
            Audio::beep(1800, 40);
        } else {
            // CD / POM: start / pause / restart-after-completion
            if (running_) {
                running_ = false;
                Audio::beep(1800, 40);
            } else {
                // If we're at zero (timer ran out, sitting idle), re-arm
                // to the current preset's full duration so A short re-starts
                // the same countdown instead of starting from 0.
                if (sub_ == SUB_COUNTDOWN && cd_remaining_ms_ <= 0) {
                    cd_remaining_ms_ = (int32_t)CD_OPTIONS_S[cd_idx_] * 1000;
                    cd_last_beep_sec_ = 99;
                } else if (sub_ == SUB_POMODORO && pom_remaining_ms_ <= 0) {
                    pom_in_break_ = false;
                    pom_remaining_ms_ = (int32_t)POM_OPTIONS[pom_idx_][0] * 60 * 1000;
                }
                start_ms_ = millis();
                running_ = true;
                Audio::beep(1800, 40);
            }
        }
    } else if (e == EV_A_LONG) {
        if (running_) {
            // reset while running: stop + zero
            running_ = false;
            Audio::beep(800, 120);
        }
        if (sub_ == SUB_SW) {
            accum_ms_ = 0;
        } else if (sub_ == SUB_COUNTDOWN) {
            // cycle duration option 30→60→120→30
            cd_idx_ = (cd_idx_ + 1) % 3;
            cd_remaining_ms_ = CD_OPTIONS_S[cd_idx_] * 1000;
            cd_last_beep_sec_ = 99;
            Audio::beepDouble(1000, 1600);
        } else if (sub_ == SUB_POMODORO) {
            pom_idx_ = (pom_idx_ + 1) % 2;
            pom_in_break_ = false;
            pom_remaining_ms_ = (int32_t)POM_OPTIONS[pom_idx_][0] * 60 * 1000;
            Audio::beepDouble(1000, 1600);
        }
    }
}

void TimerMode::renderPanel(M5Canvas& c, int x, int y, int w, int h) {
    c.fillRect(x, y, w, h, UI::COL_BG);
    c.setTextDatum(top_left);
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(UI::COL_FG, UI::COL_BG);

    char buf[16];
    uint32_t now = millis();

    if (sub_ == SUB_SW) {
        uint32_t total = accum_ms_;
        if (running_) total += now - start_ms_;
        uint32_t mm = (total / 60000) % 100;
        uint32_t ss = (total / 1000) % 60;
        uint32_t cs = (total / 10) % 100;
        c.drawString("STOPWATCH", x + 6, y + 6);
        snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)mm, (unsigned long)ss);
        c.setFont(&fonts::Font7);
        c.setTextColor(running_ ? UI::COL_OK : UI::COL_FG, UI::COL_BG);
        c.setTextDatum(top_center);
        c.drawString(buf, x + w/2, y + 30);
        // centiseconds
        snprintf(buf, sizeof(buf), ".%02lu", (unsigned long)cs);
        c.setFont(&fonts::Font4);
        c.setTextColor(UI::COL_DIM, UI::COL_BG);
        c.drawString(buf, x + w/2, y + 78);
    } else if (sub_ == SUB_COUNTDOWN) {
        int32_t rem = cd_remaining_ms_;
        if (rem < 0) rem = 0;
        uint32_t mm = (rem / 60000);
        uint32_t ss = (rem / 1000) % 60;
        c.drawString("COUNTDOWN", x + 6, y + 6);

        // Preset row — three pills side-by-side, current one highlighted.
        // Makes it obvious that 30s / 1m / 2m are all available, and that
        // long-press A cycles between them.
        const char* labels[3] = { "30s", "1m", "2m" };
        int pillW = (w - 16) / 3;
        int pillY = y + 32;
        int pillH = 22;
        for (int i = 0; i < 3; i++) {
            int px = x + 8 + i * pillW;
            bool active = (i == (int)cd_idx_);
            uint16_t bg = active ? UI::COL_ACCENT : UI::COL_PANEL;
            uint16_t fg = active ? UI::COL_BG     : UI::COL_FG;
            c.fillRoundRect(px + 1, pillY, pillW - 2, pillH, 4, bg);
            c.setFont(&fonts::Font2);
            c.setTextColor(fg, bg);
            c.setTextDatum(middle_center);
            c.drawString(labels[i], px + pillW / 2, pillY + pillH / 2 + 1);
        }

        // Remaining time
        snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)mm, (unsigned long)ss);
        uint16_t col = UI::COL_FG;
        if (running_ && ss < 4 && mm == 0) col = UI::COL_HOT;
        else if (running_) col = UI::COL_OK;
        c.setFont(&fonts::Font7);
        c.setTextColor(col, UI::COL_BG);
        c.setTextDatum(top_center);
        c.drawString(buf, x + w/2, y + 70);
    } else { // POMODORO
        int32_t rem = pom_remaining_ms_;
        if (rem < 0) rem = 0;
        uint32_t mm = (rem / 60000);
        uint32_t ss = (rem / 1000) % 60;
        c.drawString("POMODORO", x + 6, y + 6);
        c.setTextColor(pom_in_break_ ? UI::COL_COOL : UI::COL_HOT, UI::COL_BG);
        c.setTextDatum(top_right);
        c.drawString(pom_in_break_ ? "BREAK" : "FOCUS", x + w - 6, y + 6);

        snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)mm, (unsigned long)ss);
        c.setFont(&fonts::Font7);
        c.setTextColor(running_ ? UI::COL_OK : UI::COL_FG, UI::COL_BG);
        c.setTextDatum(top_center);
        c.drawString(buf, x + w/2, y + 36);

        // preset
        char preset[16];
        snprintf(preset, sizeof(preset), "%u+%u min",
                 POM_OPTIONS[pom_idx_][0], POM_OPTIONS[pom_idx_][1]);
        c.setFont(&fonts::Font2);
        c.setTextColor(UI::COL_DIM, UI::COL_BG);
        c.setTextDatum(top_center);
        c.drawString(preset, x + w/2, y + 90);
    }

    // Footer hint
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(UI::COL_DIM, UI::COL_BG);
    c.setTextDatum(top_left);
    // Context-aware footer hint per sub-mode
    const char* hint;
    if (sub_ == SUB_COUNTDOWN) {
        if (running_)                  hint = "A:pause  holdA:cycle 30s/1m/2m";
        else if (cd_remaining_ms_ <= 0) hint = "A:restart  holdA:change preset";
        else                            hint = "A:start  holdA:cycle 30s/1m/2m";
    } else if (sub_ == SUB_POMODORO) {
        if (running_)                    hint = "A:pause  holdA:cycle preset";
        else if (pom_remaining_ms_ <= 0) hint = "A:restart  holdA:cycle preset";
        else                             hint = "A:start  holdA:cycle preset";
    } else {
        hint = running_ ? "A:pause  holdA:reset"
                        : "A:start  holdA:reset";
    }
    c.drawString(hint, x + 6, y + h - 12);
}
