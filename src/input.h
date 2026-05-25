#pragma once
#include "app.h"

namespace Input {
    void begin();
    // Returns one event per call; EV_NONE when nothing happened.
    BtnEvent poll();
    // Long-press threshold (ms)
    static constexpr uint32_t LONG_PRESS_MS = 500;
}
