// nsdr.h — 10-minute guided Non-Sleep Deep Rest session scheduler.
//
// Engine: time-driven event list. Each event either fires a voice clip
// (Tingting TTS, embedded in nsdr_audio.h) or a chime.
//
// API:
//   NSDR::start() → t0 = now, idx = 0
//   NSDR::stop()  → halt + silence
//   NSDR::tick(now) → fire any due events
//   NSDR::isRunning(), NSDR::elapsedMs(), NSDR::currentPhase()
#pragma once
#include <Arduino.h>

namespace NSDR {

static constexpr uint32_t SESSION_DURATION_S = 600;  // 10 min

void  start();
void  stop();
void  tick(uint32_t now_ms);
bool  isRunning();
uint32_t elapsedMs();
const char* currentPhase();      // e.g. "INTRO", "FEET", "LEGS"
uint8_t   currentEventIdx();
uint8_t   totalEvents();

}  // namespace NSDR
