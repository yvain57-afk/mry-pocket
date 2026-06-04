// Mr.Y Pocket v0.1  —  M5StickS3 multi-mode pocket tool
//   Groups (B short cycles): UTIL  /  TIMER  /  CALM
//   B long: cycle sub-mode within current group
//   A short: primary action  |  A long: secondary action
#include <Arduino.h>
#include <M5Unified.h>

#include "app.h"
#include "ui.h"
#include "input.h"
#include "audio.h"
#include "mode_utility.h"
#include "mode_timer.h"
#include "mode_calm.h"
#include "mode_monitor.h"

// ────── Mode singletons ──────
static UtilityMode utility_;
static TimerMode   timer_;
static CalmMode    calm_;
static MonitorMode monitor_;

static Group s_group = GRP_TIMER;   // boot into TIMER (most-used)

Mode* getMode(Group g) {
    switch (g) {
        case GRP_UTILITY: return &utility_;
        case GRP_TIMER:   return &timer_;
        case GRP_CALM:    return &calm_;
        case GRP_MONITOR: return &monitor_;
        default:          return &timer_;
    }
}

Group currentGroup() { return s_group; }

void switchGroup(Group g) {
    if (g == s_group) return;
    getMode(s_group)->exit();
    s_group = g;
    getMode(s_group)->enter();
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu     = true;
    cfg.internal_spk     = true;
    cfg.internal_mic     = false;
    M5.begin(cfg);

    Input::begin();
    Audio::begin();
    UI::begin();

    getMode(s_group)->enter();
}

// Modified switchGroup helper still calls into enter()/exit(); frame redraw
// happens in the normal loop pacing via UI::renderFrame, so no explicit redraw
// needed at switch time.

static uint32_t s_last_frame = 0;

void loop() {
    M5.update();
    uint32_t now = millis();

    // ── Input ──
    BtnEvent ev;
    while ((ev = Input::poll()) != EV_NONE) {
        switch (ev) {
            case EV_B_SHORT: {
                Group next = (Group)((s_group + 1) % GRP_COUNT);
                switchGroup(next);
                Audio::beep(2200, 30);
                break;
            }
            case EV_B_LONG:
                getMode(s_group)->cycleSubMode();
                break;
            case EV_A_SHORT:
            case EV_A_LONG:
                getMode(s_group)->onEvent(ev);
                break;
            default: break;
        }
    }

    // ── Mode + audio tick ──
    getMode(s_group)->tick(now);
    Audio::tick();

    // ── Compose + push single frame ── (no flicker — full back-buffer blit)
    if (now - s_last_frame >= 33) {  // ~30 fps
        UI::renderFrame(now);
        s_last_frame = now;
    }

    delay(2);
}
