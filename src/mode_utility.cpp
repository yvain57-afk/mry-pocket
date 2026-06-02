#include "mode_utility.h"
#include "ui.h"
#include "audio.h"

constexpr uint8_t UtilityMode::BRIGHT_LEVELS[5];
constexpr uint8_t UtilityMode::VOLUME_LEVELS[5];

static const char* SUB_NAMES[] = { "BRIGHT", "VOLUME" };

void UtilityMode::enter() {
    M5.Display.setBrightness(BRIGHT_LEVELS[bright_step_]);
    Audio::setVolume(VOLUME_LEVELS[volume_step_]);
}

void UtilityMode::exit() {
    // settings persist
}

void UtilityMode::tick(uint32_t /*now_ms*/) {}

void UtilityMode::cycleSubMode() {
    sub_ = (Sub)((sub_ + 1) % SUB_COUNT);
    Audio::beep(1400, 30);
}

const char* UtilityMode::subName() const {
    return SUB_NAMES[sub_];
}

void UtilityMode::onEvent(BtnEvent e) {
    if (sub_ == SUB_BRIGHT) {
        if (e == EV_A_SHORT) {
            bright_step_ = (bright_step_ + 1) % 5;
            M5.Display.setBrightness(BRIGHT_LEVELS[bright_step_]);
            Audio::beep(1800, 40);
        } else if (e == EV_A_LONG) {
            bright_step_ = 4;  // jump to max
            M5.Display.setBrightness(BRIGHT_LEVELS[bright_step_]);
            Audio::beepDouble(1200, 1800);
        }
    } else { // SUB_VOLUME
        if (e == EV_A_SHORT) {
            volume_step_ = (volume_step_ + 1) % 5;
            Audio::setVolume(VOLUME_LEVELS[volume_step_]);
            // Audible test beep — pitched to match level so user gauges loudness
            Audio::beep(1600, 120);
        } else if (e == EV_A_LONG) {
            // jump to max
            volume_step_ = 4;
            Audio::setVolume(VOLUME_LEVELS[volume_step_]);
            Audio::beepDouble(1200, 1800);
        }
    }
}

// Shared pill-level renderer for BRIGHT and VOLUME
static void drawLevelPanel(M5Canvas& c, int x, int y, int w, int h,
                           const char* title, uint8_t step, uint8_t total,
                           uint8_t raw_value, uint16_t bar_color,
                           const char* hint_short, const char* hint_long) {
    c.fillRect(x, y, w, h, UI::COL_BG);
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(UI::COL_FG, UI::COL_BG);
    c.setTextDatum(top_left);
    c.drawString(title, x + 6, y + 6);

    // Big bar
    int barY = y + 40;
    int barW = w - 24;
    c.drawRect(x + 12, barY, barW, 22, UI::COL_DIM);
    int fill = (raw_value * (barW - 2)) / 255;
    c.fillRect(x + 13, barY + 1, fill, 20, bar_color);

    // Numeric value
    char buf[16];
    snprintf(buf, sizeof(buf), "%u / 255", raw_value);
    c.setTextColor(UI::COL_DIM, UI::COL_BG);
    c.drawString(buf, x + 12, barY + 30);

    // Step pips
    int pipY = barY + 60;
    for (int i = 0; i < total; i++) {
        int px = x + 12 + (i * (barW - 12)) / (total - 1);
        uint16_t col = (i <= step) ? bar_color : UI::COL_PANEL;
        c.fillCircle(px, pipY, 5, col);
    }

    // Hints
    c.setFont(&fonts::Font0);
    c.setTextColor(UI::COL_DIM, UI::COL_BG);
    c.drawString(hint_short, x + 6, y + h - 24);
    c.drawString(hint_long,  x + 6, y + h - 12);
}

void UtilityMode::renderPanel(M5Canvas& c, int x, int y, int w, int h) {
    if (sub_ == SUB_BRIGHT) {
        drawLevelPanel(c, x, y, w, h,
                       "BRIGHTNESS", bright_step_, 5,
                       BRIGHT_LEVELS[bright_step_], UI::COL_YELLOW,
                       "A:next step",
                       "holdA:max");
    } else {
        drawLevelPanel(c, x, y, w, h,
                       "VOLUME", volume_step_, 5,
                       VOLUME_LEVELS[volume_step_], UI::COL_COOL,
                       "A:next step (beeps)",
                       "holdA:max");
    }
}
