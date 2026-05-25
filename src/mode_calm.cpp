#include "mode_calm.h"
#include "ui.h"
#include "audio.h"
#include "nsdr.h"

static const char* SUB_NAMES[] = { "BREATH", "MEDITATE", "NOISE", "NSDR" };

// Meditation presets — minutes
static constexpr uint8_t MED_MINUTES[4] = { 5, 10, 15, 20 };
static const char* MED_LABELS[4] = { "5 min", "10 min", "15 min", "20 min" };

// Phase tables — durations match standard protocols.
// freq_start/freq_end: audio glide for that phase (0 = silence/hold)
static const CalmMode::Phase PHASES_478[] = {
    { "INHALE", 4000, 220, 440, +1 },
    { "HOLD",   7000,   0,   0,  0 },
    { "EXHALE", 8000, 440, 180, -1 },
};
static const CalmMode::Phase PHASES_BOX[] = {
    { "INHALE", 4000, 240, 380, +1 },
    { "HOLD",   4000,   0,   0,  0 },
    { "EXHALE", 4000, 380, 200, -1 },
    { "HOLD",   4000,   0,   0,  0 },
};
// Physiological sigh: double-nose-inhale + long mouth exhale.
static const CalmMode::Phase PHASES_SIGH[] = {
    { "INHALE",  1500, 300, 450, +1 },
    { "TOP-UP",   500, 450, 600, +1 },   // continues filling
    { "EXHALE", 6000, 500, 150, -1 },
};

static const char* PATTERN_LABELS[] = { "4-7-8", "BOX", "PHYSIO SIGH" };

const CalmMode::Phase* CalmMode::currentPhases(uint8_t& count) const {
    switch (pattern_) {
        case PAT_478:  count = 3; return PHASES_478;
        case PAT_BOX:  count = 4; return PHASES_BOX;
        case PAT_SIGH: count = 3; return PHASES_SIGH;
        default:       count = 0; return nullptr;
    }
}

void CalmMode::enter() {
    breath_running_ = false;
    fill_ = 0.0f;
    phase_label_ = "READY";
}

void CalmMode::exit() {
    breath_running_ = false;
    med_running_ = false;
    NSDR::stop();
    Audio::stopAll();
}

const char* CalmMode::subName() const {
    return SUB_NAMES[sub_];
}

void CalmMode::cycleSubMode() {
    // Stop everything that might be running in the current sub-mode
    Audio::stopAll();
    breath_running_ = false;
    med_running_ = false;
    NSDR::stop();
    sub_ = (Sub)((sub_ + 1) % SUB_COUNT);
    Audio::beep(1400, 30);
}

void CalmMode::tick(uint32_t now_ms) {
    // ── Meditation timer tick ──
    if (sub_ == SUB_MEDITATE && med_running_) {
        int32_t elapsed = (int32_t)(now_ms - med_start_ms_);
        int32_t half = med_total_ms_ / 2;
        if (!med_mid_fired_ && elapsed >= half) {
            med_mid_fired_ = true;
            // soft midpoint chime
            M5.Speaker.tone(1500, 250);
        }
        if (elapsed >= med_total_ms_) {
            med_running_ = false;
            // End chime: gong + bell tail
            M5.Speaker.tone(900, 500);
            delay(520);
            M5.Speaker.tone(700, 800);
        }
    }

    // ── NSDR tick ──
    if (sub_ == SUB_NSDR) {
        NSDR::tick(now_ms);
    }

    if (sub_ == SUB_BREATH && breath_running_) {
        uint8_t n;
        const Phase* phases = currentPhases(n);
        if (!phases) return;

        const Phase& p = phases[phase_idx_];
        uint32_t elapsed = now_ms - phase_start_ms_;

        if (elapsed >= p.duration_ms) {
            // advance phase
            phase_idx_++;
            if (phase_idx_ >= n) {
                phase_idx_ = 0;
                cycle_count_++;
            }
            phase_start_ms_ = now_ms;
            const Phase& np = phases[phase_idx_];
            phase_label_ = np.label;
            if (np.freq_start > 0 && np.freq_end > 0) {
                Audio::cueGlide(np.freq_start, np.freq_end, np.duration_ms);
            } else {
                Audio::cueSilence(np.duration_ms);
            }
        } else {
            // update fill animation
            float t = (float)elapsed / (float)p.duration_ms;
            if (p.fill_dir > 0)      fill_ = t;
            else if (p.fill_dir < 0) fill_ = 1.0f - t;
            // hold keeps fill_ as-is
        }
    }
}

void CalmMode::onEvent(BtnEvent e) {
    if (sub_ == SUB_BREATH) {
        if (e == EV_A_SHORT) {
            if (breath_running_) {
                breath_running_ = false;
                Audio::stopAll();
                phase_label_ = "PAUSED";
                Audio::beep(800, 80);
            } else {
                breath_running_ = true;
                phase_idx_ = 0;
                phase_start_ms_ = millis();
                cycle_count_ = 0;
                uint8_t n;
                const Phase* phases = currentPhases(n);
                phase_label_ = phases[0].label;
                Audio::cueGlide(phases[0].freq_start, phases[0].freq_end,
                                phases[0].duration_ms);
            }
        } else if (e == EV_A_LONG) {
            // cycle pattern (only when stopped)
            if (!breath_running_) {
                pattern_ = (Pattern)((pattern_ + 1) % PAT_COUNT);
                Audio::beepDouble(1000, 1600);
            }
        }
    } else if (sub_ == SUB_MEDITATE) {
        if (e == EV_A_SHORT) {
            if (med_running_) {
                med_running_ = false;
                Audio::beep(800, 100);
            } else {
                med_running_ = true;
                med_start_ms_ = millis();
                med_total_ms_ = (int32_t)MED_MINUTES[med_idx_] * 60 * 1000;
                med_mid_fired_ = false;
                // Opening gong
                M5.Speaker.tone(700, 600);
            }
        } else if (e == EV_A_LONG) {
            if (!med_running_) {
                med_idx_ = (med_idx_ + 1) % 4;
                Audio::beepDouble(1000, 1600);
            }
        }
    } else if (sub_ == SUB_NSDR) {
        if (e == EV_A_SHORT) {
            if (NSDR::isRunning()) {
                NSDR::stop();
                Audio::beep(800, 100);
            } else {
                NSDR::start();
            }
        }
        // A long does nothing in NSDR — single fixed 10-min program
    } else { // SUB_NOISE
        if (e == EV_A_SHORT) {
            if (Audio::noiseRunning()) {
                Audio::noiseStop();
                Audio::beep(800, 60);
            } else {
                Audio::NoiseType t =
                    (noise_idx_ == 0) ? Audio::NOISE_BROWN :
                    (noise_idx_ == 1) ? Audio::NOISE_PINK  :
                                        Audio::NOISE_WHITE;
                Audio::noiseStart(t);
                Audio::beep(1600, 60);
            }
        } else if (e == EV_A_LONG) {
            // cycle color
            bool was_running = Audio::noiseRunning();
            if (was_running) Audio::noiseStop();
            noise_idx_ = (noise_idx_ + 1) % 3;
            Audio::beepDouble(1000, 1600);
            if (was_running) {
                Audio::NoiseType t =
                    (noise_idx_ == 0) ? Audio::NOISE_BROWN :
                    (noise_idx_ == 1) ? Audio::NOISE_PINK  :
                                        Audio::NOISE_WHITE;
                Audio::noiseStart(t);
            }
        }
    }
}

void CalmMode::renderPanel(M5Canvas& c, int x, int y, int w, int h) {
    c.fillRect(x, y, w, h, UI::COL_BG);
    c.setTextDatum(top_left);
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(UI::COL_FG, UI::COL_BG);

    if (sub_ == SUB_BREATH) {
        c.drawString("BREATH", x + 6, y + 6);
        c.setTextColor(UI::COL_COOL, UI::COL_BG);
        c.setTextDatum(top_right);
        c.drawString(PATTERN_LABELS[pattern_], x + w - 6, y + 6);

        // Animated lung circle
        int cx = x + w / 2;
        int cy = y + 70;
        int rmin = 8;
        int rmax = 44;
        int r = rmin + (int)((rmax - rmin) * fill_);
        // Outer guide
        c.drawCircle(cx, cy, rmax, UI::COL_PANEL);
        // Filled
        uint16_t col = breath_running_ ? UI::COL_COOL : UI::COL_DIM;
        c.fillCircle(cx, cy, r, col);

        // Phase label
        c.setFont(&fonts::Font4);
        c.setTextColor(UI::COL_FG, UI::COL_BG);
        c.setTextDatum(top_center);
        c.drawString(phase_label_, cx, y + 124);

        // Cycle count
        char buf[24];
        snprintf(buf, sizeof(buf), "cycles: %u", cycle_count_);
        c.setFont(&fonts::Font0);
        c.setTextColor(UI::COL_DIM, UI::COL_BG);
        c.drawString(buf, cx, y + h - 24);

    } else if (sub_ == SUB_MEDITATE) {
        c.drawString("MEDITATE", x + 6, y + 6);
        // Preset shown top-right
        c.setTextColor(UI::COL_COOL, UI::COL_BG);
        c.setTextDatum(top_right);
        c.drawString(MED_LABELS[med_idx_], x + w - 6, y + 6);

        // Remaining time, big
        int32_t rem_ms;
        if (med_running_) {
            int32_t elapsed = (int32_t)(millis() - med_start_ms_);
            rem_ms = med_total_ms_ - elapsed;
            if (rem_ms < 0) rem_ms = 0;
        } else {
            rem_ms = (int32_t)MED_MINUTES[med_idx_] * 60 * 1000;
        }
        uint32_t mm = rem_ms / 60000;
        uint32_t ss = (rem_ms / 1000) % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)mm, (unsigned long)ss);
        c.setFont(&fonts::Font7);
        c.setTextColor(med_running_ ? UI::COL_COOL : UI::COL_FG, UI::COL_BG);
        c.setTextDatum(top_center);
        c.drawString(buf, x + w/2, y + 40);

        // Lotus dot ring (decorative) — animates softly when running
        int cx = x + w / 2;
        int cy = y + 130;
        for (int i = 0; i < 8; i++) {
            float ang = i * (2.0f * 3.14159f / 8.0f);
            int dx = (int)(20 * cosf(ang));
            int dy = (int)(20 * sinf(ang));
            uint16_t col = UI::COL_PANEL;
            if (med_running_) {
                // light up the dot at "now" position
                uint32_t e = millis() - med_start_ms_;
                int active = (e / 750) % 8;  // step every 0.75s
                if (i == active) col = UI::COL_COOL;
            }
            c.fillCircle(cx + dx, cy + dy, 3, col);
        }
    } else if (sub_ == SUB_NSDR) {
        c.drawString("NSDR", x + 6, y + 6);
        c.setTextColor(UI::COL_COOL, UI::COL_BG);
        c.setTextDatum(top_right);
        c.drawString("10 min", x + w - 6, y + 6);

        if (NSDR::isRunning()) {
            // Remaining time
            uint32_t el = NSDR::elapsedMs() / 1000;
            uint32_t rem = (NSDR::SESSION_DURATION_S > el)
                ? (NSDR::SESSION_DURATION_S - el) : 0;
            uint32_t mm = rem / 60;
            uint32_t ss = rem % 60;
            char buf[16];
            snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)mm, (unsigned long)ss);
            c.setFont(&fonts::Font7);
            c.setTextColor(UI::COL_COOL, UI::COL_BG);
            c.setTextDatum(top_center);
            c.drawString(buf, x + w/2, y + 30);

            // Current phase label
            c.setFont(&fonts::Font4);
            c.setTextColor(UI::COL_FG, UI::COL_BG);
            c.drawString(NSDR::currentPhase(), x + w/2, y + 90);

            // Progress bar
            int pbY = y + 130;
            int pbW = w - 16;
            int pbH = 12;
            c.drawRect(x + 8, pbY, pbW, pbH, UI::COL_DIM);
            int pf = (el * (pbW - 2)) / NSDR::SESSION_DURATION_S;
            if (pf > pbW - 2) pf = pbW - 2;
            c.fillRect(x + 9, pbY + 1, pf, pbH - 2, UI::COL_COOL);

            // Phase counter
            char prog[20];
            snprintf(prog, sizeof(prog), "%u/%u",
                     (unsigned)(NSDR::currentEventIdx() + 1),
                     (unsigned)NSDR::totalEvents());
            c.setFont(&fonts::Font0);
            c.setTextColor(UI::COL_DIM, UI::COL_BG);
            c.setTextDatum(top_center);
            c.drawString(prog, x + w/2, y + 150);
        } else {
            // Idle — instructions
            c.setFont(&fonts::Font4);
            c.setTextColor(UI::COL_FG, UI::COL_BG);
            c.setTextDatum(top_center);
            c.drawString("READY", x + w/2, y + 40);

            c.setFont(&fonts::Font0);
            c.setTextColor(UI::COL_DIM, UI::COL_BG);
            c.drawString("10 min guided NSDR", x + w/2, y + 80);
            c.drawString("eyes closed, lie down", x + w/2, y + 96);
            c.drawString("voice in Mandarin", x + w/2, y + 112);

            c.setTextColor(UI::COL_ACCENT, UI::COL_BG);
            c.setFont(&fonts::Font2);
            c.drawString("press A to begin", x + w/2, y + 138);
        }
    } else { // NOISE
        c.drawString("NOISE", x + 6, y + 6);
        const char* color_label = (noise_idx_ == 0) ? "BROWN" :
                                   (noise_idx_ == 1) ? "PINK"  : "WHITE";
        uint16_t color_col = (noise_idx_ == 0) ? UI::COL_ACCENT :
                              (noise_idx_ == 1) ? UI::COL_HOT : UI::COL_FG;
        c.setFont(&fonts::Font7);
        c.setTextColor(color_col, UI::COL_BG);
        c.setTextDatum(top_center);
        c.drawString(color_label, x + w/2, y + 30);

        bool playing = Audio::noiseRunning();
        int cy2 = y + 100;
        if (playing) {
            uint32_t t = millis();
            for (int i = 0; i < 5; i++) {
                int bh = 4 + ((t / 80 + i * 3) % 16);
                c.fillRect(x + w/2 - 22 + i * 10, cy2 + (16 - bh), 6, bh, UI::COL_OK);
            }
        } else {
            c.setFont(&fonts::Font2);
            c.setTextColor(UI::COL_DIM, UI::COL_BG);
            c.drawString("(stopped)", x + w/2, cy2);
        }
    }

    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(UI::COL_DIM, UI::COL_BG);
    c.setTextDatum(top_left);
    c.drawString("A:start/stop  holdA:next", x + 6, y + h - 12);
}
