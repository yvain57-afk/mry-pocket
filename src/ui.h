#pragma once
#include "app.h"

namespace UI {

// Color palette (RGB565)
static constexpr uint16_t COL_BG       = 0x0000;   // black
static constexpr uint16_t COL_FG       = 0xFFFF;   // white
static constexpr uint16_t COL_DIM      = 0x7BEF;   // grey
static constexpr uint16_t COL_ACCENT   = 0xFD20;   // orange
static constexpr uint16_t COL_HOT      = 0xF800;   // red
static constexpr uint16_t COL_OK       = 0x07E0;   // green
static constexpr uint16_t COL_COOL     = 0x05FF;   // cyan
static constexpr uint16_t COL_YELLOW   = 0xFFE0;
static constexpr uint16_t COL_PANEL    = 0x10A2;   // dark blue-grey panel

// Layout regions
static constexpr int TOP_H     = 24;   // slim status row only
static constexpr int STRIP_Y   = 208;
static constexpr int STRIP_H   = SCR_H - STRIP_Y;
static constexpr int PANEL_Y   = TOP_H;
static constexpr int PANEL_H   = STRIP_Y - TOP_H;

void begin();
// Full-frame compose: top bar + panel (delegated to current mode) + group strip,
// all drawn into an off-screen canvas, then pushed to the display in one shot.
// No flicker — call this each frame.
void renderFrame(uint32_t now);

// Accessor for the back-buffer canvas (modes that want raw access)
M5Canvas& canvas();

}  // namespace UI
