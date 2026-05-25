#include "mode_utility.h"
#include "ui.h"
#include "audio.h"

#define DISABLE_CODE_FOR_RECEIVER
#define SEND_PWM_BY_TIMER
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>

constexpr uint8_t UtilityMode::BRIGHT_LEVELS[5];

static const char* SUB_NAMES[] = { "BRIGHT", "TV-B-GONE" };

// ─────────────────────────────────────────────────────────────
//  TV-B-Gone code table — China-focused
//
//  Sources:
//   - Flipper-IRDB community captures (real-world remote scans) for
//     Samsung, Sony, LG, Hisense, TCL, Sharp, Toshiba, Panasonic,
//     Philips, JVC, Haier, FFalcon.
//   - Brands not in Flipper (Xiaomi/Skyworth/Konka/Changhong/LeTV/
//     Huawei) use educated guesses from common NEC address patterns
//     reported in user databases. Marked "*" — accuracy lower.
//
//  Order: high-market-share Chinese brands first to maximize early hit.
// ─────────────────────────────────────────────────────────────
enum IrProto : uint8_t {
    PR_SAMSUNG,       // sendSamsung(addr16, cmd16)
    PR_NEC,           // sendNEC(addr16, cmd16) — covers NEC + NECext
    PR_SONY12, PR_SONY15, PR_SONY20,
    PR_RC5, PR_RC6,
    PR_JVC,
    PR_PANASONIC,     // sendPanasonic(addr16, cmd8) — Kaseikyo with vendor=0x2002
    PR_SHARP,
    PR_LG,
};

struct TvCode {
    IrProto proto;
    uint16_t a;
    uint16_t c;
    const char* brand;
};

static const TvCode TV_CODES[] = {
    // ─── 中国畅销 (top 6 brands先打) ───
    { PR_NEC,        0xFB04, 0x08,   "Xiaomi*" },     // common Mi TV pattern
    { PR_NEC,        0x40,   0x12,   "Mi/Redmi*" },
    { PR_NEC,        0xBF00, 0xF20D, "Hisense" },     // 55E7KQ / 32A4HAU
    { PR_NEC,        0x04,   0x08,   "Hisense alt" }, // 55K3201
    { PR_NEC,        0xC7EA, 0xE817, "TCL" },         // 32S327
    { PR_NEC,        0x1F,   0x12,   "Skyworth*" },
    { PR_NEC,        0xCE,   0x10,   "Skyworth alt*" },
    { PR_NEC,        0xC0,   0x12,   "Changhong*" },
    { PR_NEC,        0x80,   0x40,   "Changhong B*" },
    { PR_NEC,        0x4C,   0x01,   "Konka*" },
    { PR_NEC,        0x1F,   0x40,   "LeTV*" },
    { PR_NEC,        0x00,   0x12,   "LeTV alt*" },
    { PR_NEC,        0x04,   0x08,   "Haier" },       // L42C1180
    { PR_NEC,        0xE6E6, 0x10,   "Huawei*" },

    // ─── Samsung ───
    { PR_SAMSUNG,    0x0707, 0x02,   "Samsung" },     // Samsung32 from Samsung.ir

    // ─── Sony 三档位宽 ───
    { PR_SONY12,     0x01,   0x15,   "Sony 12" },
    { PR_SONY15,     0x01,   0x15,   "Sony 15" },
    { PR_SONY20,     0x01,   0x15,   "Sony 20" },

    // ─── LG ───
    { PR_NEC,        0x04,   0x08,   "LG" },          // LG.ir / 32LN5406 / 24LJ4840
    { PR_LG,         0x04,   0x08,   "LG legacy" },

    // ─── Panasonic ───
    { PR_PANASONIC,  0x0080, 0xD0,   "Panasonic" },   // Kaseikyo from N2QAYB001109

    // ─── Philips ───
    { PR_RC6,        0x00,   0x0C,   "Philips RC6" }, // PhilipsTV.ir
    { PR_RC5,        0x00,   0x0C,   "Philips RC5" }, // older

    // ─── Sharp ───
    { PR_NEC,        0x7F,   0xF50A, "Sharp" },       // Aquos.ir (NECext)

    // ─── Toshiba ───
    { PR_NEC,        0x40,   0x12,   "Toshiba" },     // 32AV502U
    { PR_NEC,        0x7D02, 0xB946, "Toshiba new" }, // 43LF421U21

    // ─── JVC ───
    { PR_RC5,        0x01,   0x0C,   "JVC RC5" },     // JVC_4KTV.ir
    { PR_JVC,        0x03,   0xC0,   "JVC NEC" },

    // ─── NEC 兜底地址 (扫一波) ───
    { PR_NEC,        0x00,   0x01,   "NEC #1" },
    { PR_NEC,        0x10,   0x10,   "NEC #2" },
    { PR_NEC,        0x20,   0x12,   "NEC #3" },
    { PR_NEC,        0xE0E0, 0x40BF, "Samsung old" },
};
static constexpr uint16_t TV_CODES_COUNT = sizeof(TV_CODES) / sizeof(TV_CODES[0]);

static void fireOne(const TvCode& t) {
    switch (t.proto) {
        case PR_SAMSUNG:   IrSender.sendSamsung(t.a, t.c, 0); break;
        case PR_NEC:       IrSender.sendNEC(t.a, t.c, 0); break;
        case PR_SONY12:    IrSender.sendSony(t.a, (uint8_t)t.c, 0, 12); break;
        case PR_SONY15:    IrSender.sendSony(t.a, (uint8_t)t.c, 0, 15); break;
        case PR_SONY20:    IrSender.sendSony(t.a, (uint8_t)t.c, 0, 20); break;
        case PR_RC5:       IrSender.sendRC5((uint8_t)t.a, (uint8_t)t.c, 0, true); break;
        case PR_RC6:       IrSender.sendRC6((uint8_t)t.a, (uint8_t)t.c, 0, true); break;
        case PR_JVC:       IrSender.sendJVC((uint8_t)t.a, (uint8_t)t.c, 0); break;
        case PR_PANASONIC: IrSender.sendPanasonic(t.a, (uint8_t)t.c, 0); break;
        case PR_SHARP:     IrSender.sendSharp((uint8_t)t.a, (uint8_t)t.c, 0); break;
        case PR_LG:        IrSender.sendLG((uint8_t)t.a, t.c, 0); break;
    }
    // park IR pin low after send to keep LED off
    pinMode(PIN_IR, OUTPUT);
    digitalWrite(PIN_IR, LOW);
}

// ─────────────────────────────────────────────────────────────
//  UtilityMode lifecycle
// ─────────────────────────────────────────────────────────────
void UtilityMode::enter() {
    IrSender.begin(PIN_IR, false);
    M5.Display.setBrightness(BRIGHT_LEVELS[bright_step_]);
    tvbg_total_ = TV_CODES_COUNT;
}

void UtilityMode::exit() {
    tvbg_running_ = false;
}

void UtilityMode::tick(uint32_t now_ms) {
    if (sub_ == SUB_TVBGONE && tvbg_running_) {
        // Fire ~3 codes per second. Each protocol send is blocking ~50-80ms,
        // we add a small gap so TVs have time to decode and react.
        if (now_ms - tvbg_last_ms_ >= 280) {
            if (tvbg_idx_ >= TV_CODES_COUNT) {
                // Done — celebratory triple beep
                tvbg_running_ = false;
                tvbg_idx_ = 0;
                Audio::beep(2400, 80);
                delay(100);
                Audio::beep(2800, 80);
                delay(100);
                Audio::beep(3200, 160);
            } else {
                fireOne(TV_CODES[tvbg_idx_]);
                tvbg_idx_++;
                tvbg_last_ms_ = now_ms;
            }
        }
    }
}

void UtilityMode::cycleSubMode() {
    if (tvbg_running_) {
        // abort sweep on sub-mode switch
        tvbg_running_ = false;
        tvbg_idx_ = 0;
    }
    sub_ = (Sub)((sub_ + 1) % SUB_COUNT);
    Audio::beep(1400, 30);
}

const char* UtilityMode::subName() const {
    return SUB_NAMES[sub_];
}

void UtilityMode::onEvent(BtnEvent e) {
    switch (e) {
        case EV_A_SHORT:
            if (sub_ == SUB_BRIGHT) {
                bright_step_ = (bright_step_ + 1) % 5;
                M5.Display.setBrightness(BRIGHT_LEVELS[bright_step_]);
                Audio::beep(1800, 40);
            } else if (sub_ == SUB_TVBGONE) {
                if (tvbg_running_) {
                    // abort
                    tvbg_running_ = false;
                    tvbg_idx_ = 0;
                    Audio::beep(600, 120);
                } else {
                    // start sweep
                    tvbg_running_ = true;
                    tvbg_idx_ = 0;
                    tvbg_last_ms_ = 0;  // fire first immediately on next tick
                    Audio::beepDouble(2400, 3000);
                }
            }
            break;
        case EV_A_LONG:
            // brightness: jump to max; tv-b-gone: abort
            if (sub_ == SUB_BRIGHT) {
                bright_step_ = 4;
                M5.Display.setBrightness(BRIGHT_LEVELS[bright_step_]);
                Audio::beepDouble(1200, 1800);
            } else if (sub_ == SUB_TVBGONE) {
                tvbg_running_ = false;
                tvbg_idx_ = 0;
                Audio::beep(600, 120);
            }
            break;
        default: break;
    }
}

void UtilityMode::renderPanel(M5Canvas& c, int x, int y, int w, int h) {
    c.fillRect(x, y, w, h, UI::COL_BG);
    c.setTextDatum(top_left);
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(UI::COL_FG, UI::COL_BG);

    if (sub_ == SUB_BRIGHT) {
        c.drawString("BRIGHTNESS", x + 6, y + 6);
        int barY = y + 40;
        int barW = w - 24;
        c.drawRect(x + 12, barY, barW, 18, UI::COL_DIM);
        int fill = (BRIGHT_LEVELS[bright_step_] * barW) / 255;
        c.fillRect(x + 12, barY, fill, 18, UI::COL_YELLOW);

        char buf[16];
        snprintf(buf, sizeof(buf), "%u / 255", BRIGHT_LEVELS[bright_step_]);
        c.setTextColor(UI::COL_DIM, UI::COL_BG);
        c.drawString(buf, x + 12, barY + 28);

        for (int i = 0; i < 5; i++) {
            int px = x + 12 + (i * (barW - 12)) / 4;
            int py = barY + 60;
            uint16_t pipCol = (i <= bright_step_) ? UI::COL_YELLOW : UI::COL_PANEL;
            c.fillCircle(px, py, 4, pipCol);
        }

        c.setFont(&fonts::Font0);
        c.setTextColor(UI::COL_DIM, UI::COL_BG);
        c.drawString("A:next  holdA:max", x + 6, y + h - 12);
    } else {
        // ── TV-B-Gone panel ──
        c.setTextColor(UI::COL_HOT, UI::COL_BG);
        c.drawString("TV-B-GONE", x + 6, y + 6);

        c.setFont(&fonts::Font2);
        if (tvbg_running_) {
            // Big "FIRING" + progress
            c.setFont(&fonts::Font4);
            c.setTextColor(UI::COL_HOT, UI::COL_BG);
            c.setTextDatum(top_center);
            c.drawString("FIRING", x + w/2, y + 36);

            // progress bar
            int pbY = y + 78;
            int pbW = w - 16;
            int pbH = 14;
            c.drawRect(x + 8, pbY, pbW, pbH, UI::COL_DIM);
            int pf = (tvbg_idx_ * (pbW - 2)) / TV_CODES_COUNT;
            c.fillRect(x + 9, pbY + 1, pf, pbH - 2, UI::COL_HOT);

            // current brand + counter
            char prog[28];
            snprintf(prog, sizeof(prog), "%u/%u",
                     (unsigned)tvbg_idx_, (unsigned)TV_CODES_COUNT);
            c.setFont(&fonts::Font2);
            c.setTextColor(UI::COL_FG, UI::COL_BG);
            c.setTextDatum(top_center);
            c.drawString(prog, x + w/2, y + 100);

            const char* brand = (tvbg_idx_ > 0 && tvbg_idx_ <= TV_CODES_COUNT)
                ? TV_CODES[tvbg_idx_ - 1].brand : "";
            c.setFont(&fonts::Font0);
            c.setTextColor(UI::COL_DIM, UI::COL_BG);
            c.drawString(brand, x + w/2, y + 124);

            c.setFont(&fonts::Font0);
            c.setTextColor(UI::COL_DIM, UI::COL_BG);
            c.setTextDatum(top_left);
            c.drawString("A: abort", x + 6, y + h - 12);
        } else {
            // Idle — instructions
            c.setFont(&fonts::Font4);
            c.setTextColor(UI::COL_FG, UI::COL_BG);
            c.setTextDatum(top_center);
            c.drawString("READY", x + w/2, y + 36);

            c.setFont(&fonts::Font0);
            c.setTextColor(UI::COL_DIM, UI::COL_BG);
            c.drawString("aim LED at TV", x + w/2, y + 70);
            c.drawString("range < 2m", x + w/2, y + 84);

            char codes[20];
            snprintf(codes, sizeof(codes), "%u codes loaded", (unsigned)TV_CODES_COUNT);
            c.drawString(codes, x + w/2, y + 110);

            c.setTextColor(UI::COL_ACCENT, UI::COL_BG);
            c.setFont(&fonts::Font2);
            c.drawString("press A to fire", x + w/2, y + 140);

            c.setFont(&fonts::Font0);
            c.setTextColor(UI::COL_DIM, UI::COL_BG);
            c.setTextDatum(top_left);
            c.drawString("A: fire all  holdA: stop", x + 6, y + h - 12);
        }
    }
}
