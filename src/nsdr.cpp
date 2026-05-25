#include "nsdr.h"
#include "nsdr_audio.h"
#include "audio.h"
#include <M5Unified.h>

namespace NSDR {

// Reserve speaker channel 6 for NSDR voice playback so it doesn't fight
// with brown-noise (ch7) or tone cues (auto-assigned channels).
static constexpr int VOICE_CHANNEL = 6;

// Schedule — each entry: time offset (seconds), clip id, phase label.
// clip = -1 means "no clip" (chime / silence beat).
struct Event {
    uint16_t t_s;
    int8_t   clip;       // index into nsdr_clips[], or -1
    const char* phase;
};

static const Event SCHEDULE[] = {
    {   0, NSDR_INTRO,   "INTRO"   },
    {  30, NSDR_BREATHE, "BREATH"  },
    {  60, NSDR_FEET,    "FEET"    },
    { 120, NSDR_LEGS,    "LEGS"    },
    { 180, NSDR_BELLY,   "BELLY"   },
    { 240, NSDR_CHEST,   "CHEST"   },
    { 300, NSDR_ARMS,    "ARMS"    },
    { 360, NSDR_NECK,    "NECK"    },
    { 420, NSDR_FACE,    "FACE"    },
    { 480, NSDR_WHOLE,   "WHOLE"   },
    { 540, NSDR_RETURN,  "RETURN"  },
    { 580, NSDR_END,     "WAKE"    },
    { 596, -1,           "CHIME"   },   // end gong
};
static constexpr uint8_t SCHEDULE_COUNT = sizeof(SCHEDULE) / sizeof(SCHEDULE[0]);

static bool      s_running = false;
static uint32_t  s_start_ms = 0;
static uint8_t   s_next_idx = 0;
static const char* s_phase_label = "READY";
static uint8_t   s_played_idx = 0;       // last played event index (for UI)

void start() {
    s_running = true;
    s_start_ms = millis();
    s_next_idx = 0;
    s_played_idx = 0;
    s_phase_label = "STARTING";
}

void stop() {
    s_running = false;
    M5.Speaker.stop(VOICE_CHANNEL);
    s_phase_label = "STOPPED";
}

bool isRunning() { return s_running; }
uint32_t elapsedMs() { return s_running ? (millis() - s_start_ms) : 0; }
const char* currentPhase() { return s_phase_label; }
uint8_t currentEventIdx() { return s_played_idx; }
uint8_t totalEvents() { return SCHEDULE_COUNT; }

static void fireEndGong() {
    // Three slow bells, descending — signals session end
    M5.Speaker.tone(1200, 400);
    delay(420);
    M5.Speaker.tone(900,  400);
    delay(420);
    M5.Speaker.tone(600,  600);
}

void tick(uint32_t now_ms) {
    if (!s_running) return;

    uint32_t elapsed_s = (now_ms - s_start_ms) / 1000;

    // Fire all events that have come due
    while (s_next_idx < SCHEDULE_COUNT && elapsed_s >= SCHEDULE[s_next_idx].t_s) {
        const Event& e = SCHEDULE[s_next_idx];
        s_phase_label = e.phase;
        s_played_idx = s_next_idx;

        if (e.clip >= 0 && e.clip < NSDR_CLIP_COUNT) {
            const NsdrClip& clip = nsdr_clips[e.clip];
            M5.Speaker.playWav(clip.data, clip.len, 1, VOICE_CHANNEL, true);
        } else {
            // chime event
            fireEndGong();
        }
        s_next_idx++;
    }

    // Auto-end after the full duration
    if (elapsed_s >= SESSION_DURATION_S) {
        s_running = false;
        s_phase_label = "DONE";
    }
}

}  // namespace NSDR
