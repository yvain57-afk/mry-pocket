#include "audio.h"
#include <math.h>

namespace Audio {

// ───────── Glide tone state (non-blocking) ─────────
static bool     g_glide_on = false;
static uint32_t g_glide_start = 0;
static uint32_t g_glide_dur   = 0;
static uint16_t g_glide_f0    = 0;
static uint16_t g_glide_f1    = 0;
static uint16_t g_glide_step_ms = 80;   // re-trigger tone every 80ms
static uint32_t g_glide_last_step = 0;

// ───────── Noise state ─────────
static NoiseType g_noise = NOISE_NONE;
static int32_t   g_brown_acc = 0;   // brown-noise integrator
static float     g_pink_b[7] = {0}; // Paul Kellet pink filter

static constexpr uint32_t NOISE_SR        = 16000;  // sample rate
static constexpr size_t   NOISE_CHUNK     = 512;    // ~32ms per chunk
// Double-buffer: M5 speaker keeps a 2-slot queue per channel. We must NOT
// overwrite a buffer while it might still be playing — alternate between two.
static int16_t   g_noise_buf[2][NOISE_CHUNK];
static uint8_t   g_noise_buf_idx = 0;

static uint8_t g_volume = 220;   // 0..255 — speaker is tiny, push it

void begin() {
    auto cfg = M5.Speaker.config();
    cfg.sample_rate = NOISE_SR;
    M5.Speaker.config(cfg);
    M5.Speaker.begin();
    M5.Speaker.setVolume(g_volume);
    // M5Unified speaker has TWO gain stages: master_volume × channel_volume.
    // The per-channel default is conservative and was muting WAV playback
    // (NSDR voice). Pin all 8 channels to max so master_volume is the only knob.
    M5.Speaker.setAllChannelVolume(255);
}

void setVolume(uint8_t v) { g_volume = v; M5.Speaker.setVolume(v); }
uint8_t getVolume() { return g_volume; }

void beep(uint16_t freq, uint16_t ms) {
    M5.Speaker.tone(freq, ms);
}

void beepDouble(uint16_t f1, uint16_t f2) {
    M5.Speaker.tone(f1, 100);
    delay(120);
    M5.Speaker.tone(f2, 100);
}

void stopAll() {
    M5.Speaker.stop();
    g_glide_on = false;
    g_noise = NOISE_NONE;
}

void cueGlide(uint16_t f_start, uint16_t f_end, uint32_t duration_ms) {
    g_glide_on = true;
    g_glide_start = millis();
    g_glide_dur = duration_ms;
    g_glide_f0 = f_start;
    g_glide_f1 = f_end;
    g_glide_last_step = 0;
}

void cueSilence(uint32_t /*duration_ms*/) {
    g_glide_on = false;
    M5.Speaker.stop();
}

void noiseStart(NoiseType type) {
    g_noise = type;
    g_brown_acc = 0;
    for (auto& b : g_pink_b) b = 0;
}

void noiseStop() {
    g_noise = NOISE_NONE;
    M5.Speaker.stop();
}

bool noiseRunning() { return g_noise != NOISE_NONE; }

static void fillWhite(int16_t* buf, size_t n) {
    // Full-amplitude white noise — fills the int16 range
    for (size_t i = 0; i < n; i++) {
        buf[i] = (int16_t)((esp_random() & 0xFFFF) - 32768);
    }
}

static void fillBrown(int16_t* buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int32_t r = (int32_t)(esp_random() & 0x3FF) - 512;  // ±512
        g_brown_acc = (g_brown_acc * 63 / 64) + r;          // leaky integrator
        // Push closer to int16 max for audibility
        if (g_brown_acc >  30000) g_brown_acc =  30000;
        if (g_brown_acc < -30000) g_brown_acc = -30000;
        buf[i] = (int16_t)g_brown_acc;
    }
}

// Paul Kellet pink-noise filter
static void fillPink(int16_t* buf, size_t n) {
    auto* b = g_pink_b;
    for (size_t i = 0; i < n; i++) {
        float white = ((int32_t)(esp_random() & 0xFFFF) - 32768) / 32768.0f;
        b[0] = 0.99886f * b[0] + white * 0.0555179f;
        b[1] = 0.99332f * b[1] + white * 0.0750759f;
        b[2] = 0.96900f * b[2] + white * 0.1538520f;
        b[3] = 0.86650f * b[3] + white * 0.3104856f;
        b[4] = 0.55000f * b[4] + white * 0.5329522f;
        b[5] = -0.7616f * b[5] - white * 0.0168980f;
        float pink = b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6] + white*0.5362f;
        b[6] = white * 0.115926f;
        pink *= 0.11f;
        int32_t s = (int32_t)(pink * 30000);
        if (s >  30000) s =  30000;
        if (s < -30000) s = -30000;
        buf[i] = (int16_t)s;
    }
}

void tick() {
    uint32_t now = millis();

    // ── Glide cue: re-trigger tone() every step_ms with interpolated frequency
    if (g_glide_on) {
        uint32_t elapsed = now - g_glide_start;
        if (elapsed >= g_glide_dur) {
            g_glide_on = false;
        } else if (now - g_glide_last_step >= g_glide_step_ms) {
            float t = (float)elapsed / (float)g_glide_dur;
            uint16_t f = (uint16_t)(g_glide_f0 + (int32_t)(g_glide_f1 - g_glide_f0) * t);
            M5.Speaker.tone(f, g_glide_step_ms + 20);  // small overlap to avoid gap
            g_glide_last_step = now;
        }
    }

    // ── Noise: keep both queue slots on channel 7 filled with fresh chunks
    if (g_noise != NOISE_NONE) {
        // isPlaying(ch) returns 0/1/2 — number of queued plays. Top up to 2.
        while (M5.Speaker.isPlaying(7) < 2) {
            int16_t* b = g_noise_buf[g_noise_buf_idx];
            switch (g_noise) {
                case NOISE_BROWN: fillBrown(b, NOISE_CHUNK); break;
                case NOISE_PINK:  fillPink (b, NOISE_CHUNK); break;
                case NOISE_WHITE: fillWhite(b, NOISE_CHUNK); break;
                default: return;
            }
            if (!M5.Speaker.playRaw(b, NOISE_CHUNK, NOISE_SR,
                                    false /*stereo*/, 1 /*repeat*/, 7 /*channel*/)) {
                break;  // queue rejected — try again next tick
            }
            g_noise_buf_idx = 1 - g_noise_buf_idx;
        }
    }
}

}  // namespace Audio
