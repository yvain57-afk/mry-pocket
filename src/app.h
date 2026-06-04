// app.h — shared types and global state for Mr.Y Pocket
#pragma once
#include <Arduino.h>
#include <M5Unified.h>

// ───────── Hardware pins (M5StickS3) ─────────
static constexpr uint8_t PIN_BTN_A = 11;  // main button (front, big)
static constexpr uint8_t PIN_BTN_B = 12;  // side button
static constexpr uint8_t PIN_IR    = 46;  // IR LED

// ───────── Screen ─────────
static constexpr uint16_t SCR_W = 135;
static constexpr uint16_t SCR_H = 240;

// ───────── Mode groups ─────────
enum Group : uint8_t {
    GRP_UTILITY = 0,   // brightness + volume
    GRP_TIMER   = 1,   // stopwatch + countdown + pomodoro
    GRP_CALM    = 2,   // breathing + meditate + noise + NSDR
    GRP_MONITOR = 3,   // Codex usage from local Macs
    GRP_COUNT
};

// ───────── Button events ─────────
enum BtnEvent : uint8_t {
    EV_NONE      = 0,
    EV_A_SHORT   = 1,
    EV_A_LONG    = 2,
    EV_B_SHORT   = 3,
    EV_B_LONG    = 4,
};

// ───────── Mode interface ─────────
struct Mode {
    virtual ~Mode() = default;
    virtual void enter() {}
    virtual void exit()  {}
    virtual void tick(uint32_t now_ms) {}
    virtual void onEvent(BtnEvent e) {}
    virtual const char* name() const = 0;    // "UTILITY"
    virtual const char* subName() const = 0; // "BRIGHT" / "IR" / etc
    // Cycle through sub-modes within this group (called on B long-press)
    virtual void cycleSubMode() {}
    // Renders the central panel (between top bar y=52 and group strip y=200)
    // INTO AN OFF-SCREEN CANVAS — never to the display directly. UI::renderFrame
    // pushes the canvas to the LCD in one shot to avoid flicker.
    virtual void renderPanel(M5Canvas& c, int x, int y, int w, int h) = 0;
};

Mode* getMode(Group g);
void  switchGroup(Group g);
Group currentGroup();
