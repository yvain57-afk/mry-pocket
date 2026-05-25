#pragma once
#include "app.h"

class CalmMode : public Mode {
public:
    void enter() override;
    void exit() override;
    void tick(uint32_t now_ms) override;
    void onEvent(BtnEvent e) override;
    void cycleSubMode() override;
    const char* name() const override { return "CALM"; }
    const char* subName() const override;
    void renderPanel(M5Canvas& c, int x, int y, int w, int h) override;

    // Phase descriptor — public so the static phase tables in the .cpp can
    // reference the type at file scope.
    struct Phase {
        const char* label;
        uint16_t    duration_ms;
        uint16_t    freq_start;
        uint16_t    freq_end;
        int8_t      fill_dir;   // -1 shrink, 0 hold, +1 grow
    };

private:
    enum Sub : uint8_t {
        SUB_BREATH   = 0,
        SUB_MEDITATE = 1,
        SUB_NOISE    = 2,
        SUB_NSDR     = 3,
        SUB_COUNT
    };
    Sub sub_ = SUB_BREATH;

    // ── Meditation timer ──
    uint8_t  med_idx_      = 1;   // 0=5min, 1=10min, 2=15min, 3=20min
    bool     med_running_  = false;
    uint32_t med_start_ms_ = 0;
    int32_t  med_total_ms_ = 0;
    bool     med_mid_fired_ = false;

    // ── Breathing ──
    enum Pattern : uint8_t {
        PAT_478 = 0,   // 4-7-8
        PAT_BOX = 1,   // box 4-4-4-4
        PAT_SIGH = 2,  // physiological sigh
        PAT_COUNT
    };
    Pattern pattern_ = PAT_478;
    bool     breath_running_ = false;
    uint8_t  phase_idx_ = 0;
    uint32_t phase_start_ms_ = 0;
    uint16_t cycle_count_ = 0;

    // Visual: lung fill 0.0 (empty) .. 1.0 (full)
    float    fill_ = 0.0f;
    const char* phase_label_ = "";

    // ── Noise ──
    uint8_t  noise_idx_ = 0;  // 0=brown, 1=pink, 2=white

    const Phase* currentPhases(uint8_t& count) const;
};
