#include "input.h"

namespace Input {

struct BtnState {
    uint8_t pin;
    bool    pressed;        // raw pressed (active low)
    uint32_t press_start;   // ms when press began
    bool    long_emitted;
};

static BtnState A_ = { PIN_BTN_A, false, 0, false };
static BtnState B_ = { PIN_BTN_B, false, 0, false };

static BtnEvent processBtn(BtnState& s, BtnEvent shortEv, BtnEvent longEv) {
    bool now = (digitalRead(s.pin) == LOW);
    uint32_t t = millis();

    if (now && !s.pressed) {
        // press edge
        s.pressed = true;
        s.press_start = t;
        s.long_emitted = false;
    } else if (now && s.pressed) {
        // held — fire long press once threshold passed
        if (!s.long_emitted && (t - s.press_start) >= LONG_PRESS_MS) {
            s.long_emitted = true;
            return longEv;
        }
    } else if (!now && s.pressed) {
        // release edge
        s.pressed = false;
        if (!s.long_emitted && (t - s.press_start) >= 30) {
            // debounced short press
            return shortEv;
        }
    }
    return EV_NONE;
}

void begin() {
    pinMode(PIN_BTN_A, INPUT_PULLUP);
    pinMode(PIN_BTN_B, INPUT_PULLUP);
}

BtnEvent poll() {
    BtnEvent ev = processBtn(A_, EV_A_SHORT, EV_A_LONG);
    if (ev != EV_NONE) return ev;
    return processBtn(B_, EV_B_SHORT, EV_B_LONG);
}

}  // namespace Input
