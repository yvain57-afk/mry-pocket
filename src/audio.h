// audio.h — speaker abstraction: tones, sweeps, noise generators, breathing cues.
// Designed to be non-blocking: call Audio::tick() each loop.
#pragma once
#include "app.h"

namespace Audio {

enum NoiseType : uint8_t {
    NOISE_NONE  = 0,
    NOISE_BROWN = 1,   // bassy, calming — default
    NOISE_PINK  = 2,   // 1/f-ish, balanced
    NOISE_WHITE = 3,   // bright, harsh on tiny speaker
};

void begin();
void tick();                      // call every loop iteration

// Simple beeps
void beep(uint16_t freq, uint16_t ms);
void beepDouble(uint16_t f1, uint16_t f2);
void stopAll();

// Breathing audio cues — non-blocking, kicked off then advanced by tick().
// freq_start → freq_end linear glide over duration_ms while breathing.
void cueGlide(uint16_t f_start, uint16_t f_end, uint32_t duration_ms);
void cueSilence(uint32_t duration_ms);

// Continuous noise — toggled on/off, generated in tick() in chunks.
void noiseStart(NoiseType type);
void noiseStop();
bool noiseRunning();

// Volume 0..255 (M5Unified scale)
void setVolume(uint8_t v);
uint8_t getVolume();

}  // namespace Audio
