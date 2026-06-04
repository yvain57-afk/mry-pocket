#include "ui.h"

namespace UI {

static uint32_t s_boot_ms = 0;
static const char* GROUP_LABELS[GRP_COUNT] = { "UTIL", "TIME", "CALM", "MON" };

// ── Off-screen canvas (16-bit, in PSRAM) — full-screen back buffer ──
static M5Canvas s_canvas(&M5.Display);
M5Canvas& canvas() { return s_canvas; }

void begin() {
    M5.Display.setRotation(0);
    M5.Display.fillScreen(COL_BG);

    s_canvas.setPsram(true);
    s_canvas.setColorDepth(16);
    s_canvas.createSprite(SCR_W, SCR_H);
    s_canvas.setSwapBytes(false);

    s_boot_ms = millis();
}

// ── Top bar (y 0..24) — single slim status row ──
//   Left: current group/sub label (orange)
//   Right: battery icon + percent (color-coded)
static void drawTop(M5Canvas& c, uint32_t /*now*/) {
    c.fillRect(0, 0, SCR_W, TOP_H, COL_BG);

    // Mode label on the left
    Mode* m = getMode(currentGroup());
    char label[32];
    snprintf(label, sizeof(label), "%s\xB7%s", m->name(), m->subName());  // 0xB7 = ·
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(COL_ACCENT, COL_BG);
    c.setTextDatum(middle_left);
    c.drawString(label, 4, TOP_H / 2);

    // Battery icon on the right
    int batX = SCR_W - 26;
    int batY = (TOP_H - 10) / 2;
    c.drawRect(batX, batY, 22, 10, COL_FG);
    c.fillRect(batX + 22, batY + 3, 2, 4, COL_FG);
    int lvl = M5.Power.getBatteryLevel();
    if (lvl < 0) lvl = 0; if (lvl > 100) lvl = 100;
    int fill = (lvl * 18) / 100;
    uint16_t batCol = (lvl < 20) ? COL_HOT : (lvl < 50 ? COL_ACCENT : COL_OK);
    c.fillRect(batX + 2, batY + 2, fill, 6, batCol);

    c.drawFastHLine(0, TOP_H - 1, SCR_W, COL_DIM);
}

// ── Group strip (bottom y 200..240) ──
static void drawGroupStrip(M5Canvas& c) {
    c.fillRect(0, STRIP_Y, SCR_W, STRIP_H, COL_BG);
    c.drawFastHLine(0, STRIP_Y, SCR_W, COL_DIM);

    int tileW = SCR_W / GRP_COUNT;
    int tileH = STRIP_H - 4;
    int y = STRIP_Y + 4;
    Group cur = currentGroup();
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextDatum(middle_center);
    for (uint8_t i = 0; i < GRP_COUNT; i++) {
        int x = i * tileW;
        bool active = (i == cur);
        uint16_t fg = active ? COL_BG : COL_FG;
        uint16_t bg = active ? COL_ACCENT : COL_PANEL;
        c.fillRoundRect(x + 2, y, tileW - 4, tileH, 4, bg);
        c.setTextColor(fg, bg);
        c.drawString(GROUP_LABELS[i], x + tileW / 2, y + tileH / 2 + 1);
    }
}

void renderFrame(uint32_t now) {
    // 1. Compose entire frame off-screen
    s_canvas.fillScreen(COL_BG);
    drawTop(s_canvas, now);
    s_canvas.drawRoundRect(2, PANEL_Y + 2, SCR_W - 4, PANEL_H - 4, 6, COL_DIM);
    Mode* m = getMode(currentGroup());
    m->renderPanel(s_canvas, 4, PANEL_Y + 4, SCR_W - 8, PANEL_H - 8);
    drawGroupStrip(s_canvas);

    // 2. Single blit to LCD (no flicker)
    s_canvas.pushSprite(0, 0);
}

}  // namespace UI
