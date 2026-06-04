#include "mode_monitor.h"
#include "ui.h"
#include "audio.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

using WiFiConfig::ENDPOINTS;
using WiFiConfig::ENDPOINT_COUNT;
using WiFiConfig::POLL_INTERVAL_MS;

void MonitorMode::enter() {
    startWifiIfNeeded();
}

void MonitorMode::exit() {
    // Keep WiFi alive across modes
}

const char* MonitorMode::subName() const {
    return ENDPOINTS[sub_].label;
}

void MonitorMode::cycleSubMode() {
    sub_ = (sub_ + 1) % ENDPOINT_COUNT;
    Audio::beep(1400, 30);
    pages_[sub_].last_fetch_ms = 0;
}

void MonitorMode::onEvent(BtnEvent e) {
    if (e == EV_A_SHORT) {
        pages_[sub_].last_fetch_ms = 0;
        Audio::beep(1800, 30);
    } else if (e == EV_A_LONG) {
        cycleSubMode();
    }
}

void MonitorMode::startWifiIfNeeded() {
    if (wifi_state_ == WF_READY && WiFi.status() == WL_CONNECTED) return;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WiFiConfig::SSID, WiFiConfig::PASSWORD);
    wifi_state_ = WF_CONNECTING;
    wifi_attempt_started_ms_ = millis();
}

void MonitorMode::tick(uint32_t now_ms) {
    switch (wifi_state_) {
        case WF_BOOT:
        case WF_FAILED:
            startWifiIfNeeded();
            break;
        case WF_CONNECTING:
            if (WiFi.status() == WL_CONNECTED)
                wifi_state_ = WF_READY;
            else if (now_ms - wifi_attempt_started_ms_ > WiFiConfig::WIFI_CONNECT_TIMEOUT_MS)
                wifi_state_ = WF_FAILED;
            break;
        case WF_READY:
            if (WiFi.status() != WL_CONNECTED) {
                wifi_state_ = WF_CONNECTING;
                wifi_attempt_started_ms_ = now_ms;
            }
            break;
    }
    if (wifi_state_ != WF_READY) return;

    PageState& p = pages_[sub_];
    if (p.last_fetch_ms == 0 || (now_ms - p.last_fetch_ms) >= POLL_INTERVAL_MS) {
        doFetch(sub_, now_ms);
    }
}

void MonitorMode::doFetch(uint8_t idx, uint32_t now_ms) {
    const auto& ep = ENDPOINTS[idx];
    PageState& p = pages_[idx];
    p.last_fetch_ms = now_ms;

    String url = String("http://") + ep.host + ":" + ep.port + ep.path;

    HTTPClient http;
    http.setConnectTimeout(2500);
    http.setTimeout(3500);
    if (!http.begin(url)) {
        p.last_fetch_ok = false;
        snprintf(p.err_msg, sizeof(p.err_msg), "begin failed");
        return;
    }

    int code = http.GET();
    if (code != 200) {
        p.last_fetch_ok = false;
        snprintf(p.err_msg, sizeof(p.err_msg), "HTTP %d", code);
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    StaticJsonDocument<640> filter;
    filter["stale_seconds"]                              = true;
    filter["data"]["primary"]["used_percent"]            = true;
    filter["data"]["primary"]["resets_at"]               = true;
    filter["data"]["primary"]["window_already_reset"]    = true;
    filter["data"]["secondary"]["used_percent"]          = true;
    filter["data"]["secondary"]["resets_at"]             = true;
    filter["data"]["secondary"]["window_already_reset"]  = true;

    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(
        doc, body, DeserializationOption::Filter(filter));
    if (err) {
        p.last_fetch_ok = false;
        snprintf(p.err_msg, sizeof(p.err_msg), "json: %s", err.c_str());
        return;
    }

    JsonObject data = doc["data"];
    if (data.isNull()) {
        p.last_fetch_ok = false;
        snprintf(p.err_msg, sizeof(p.err_msg), "no data");
        return;
    }

    p.primary_pct       = data["primary"]["used_percent"]   | 0.0f;
    p.secondary_pct     = data["secondary"]["used_percent"] | 0.0f;
    p.primary_resets    = (uint32_t)(data["primary"]["resets_at"]   | 0UL);
    p.secondary_resets  = (uint32_t)(data["secondary"]["resets_at"] | 0UL);
    p.primary_rolled    = data["primary"]["window_already_reset"]   | false;
    p.secondary_rolled  = data["secondary"]["window_already_reset"] | false;
    p.stale_seconds     = (uint32_t)(doc["stale_seconds"] | 0UL);
    p.have_data         = true;
    p.last_fetch_ok     = true;
    p.err_msg[0]        = 0;
}

// ─────────── Drawing helpers ───────────

// Format a Unix epoch (UTC) as HH:MM in Asia/Shanghai (+8).
static void formatHHMM(char* buf, size_t n, uint32_t epoch) {
    if (epoch == 0) { snprintf(buf, n, "--:--"); return; }
    uint32_t local = epoch + 8 * 3600;
    uint32_t hh = (local / 3600) % 24;
    uint32_t mm = (local / 60) % 60;
    snprintf(buf, n, "%02lu:%02lu", (unsigned long)hh, (unsigned long)mm);
}

// Format duration to a compact "Xm" / "Xh" / "Xd"
static void formatAge(char* buf, size_t n, uint32_t seconds) {
    if (seconds < 60)         snprintf(buf, n, "%lus", (unsigned long)seconds);
    else if (seconds < 3600)  snprintf(buf, n, "%lum", (unsigned long)(seconds / 60));
    else if (seconds < 86400) snprintf(buf, n, "%luh", (unsigned long)(seconds / 3600));
    else                      snprintf(buf, n, "%lud", (unsigned long)(seconds / 86400));
}

// Draw one usage block: small label, full-width bar, percentage right, reset HH:MM left
struct BlockSpec {
    int  y0;
    const char* label;
    float pct;
    bool is_stale;          // > threshold OR window has rolled over
    bool rolled;
    uint32_t resets_at;
    bool is_weekly;         // weekly resets show "M/D" instead of HH:MM
};

static void drawBlock(M5Canvas& c, int x, int w, const BlockSpec& b) {
    // Label
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(UI::COL_FG, UI::COL_BG);
    c.setTextDatum(top_left);
    c.drawString(b.label, x + 6, b.y0);

    // Bar
    int barX = x + 6;
    int barY = b.y0 + 20;
    int barW = w - 12;
    int barH = 16;
    c.drawRoundRect(barX, barY, barW, barH, 3, UI::COL_DIM);

    // Fill — always green/cyan, never red. Stale → just outline.
    if (!b.is_stale) {
        int fill = (int)(b.pct * (barW - 4) / 100.0f);
        if (fill < 0) fill = 0;
        if (fill > barW - 4) fill = barW - 4;
        uint16_t fg = (b.pct >= 75) ? UI::COL_COOL : UI::COL_OK;
        c.fillRoundRect(barX + 2, barY + 2, fill, barH - 4, 2, fg);
    } else {
        // Subtle hatched look — draw evenly-spaced dim dots to signal "unknown"
        for (int dx = 4; dx < barW - 4; dx += 6) {
            c.drawFastVLine(barX + dx, barY + 5, barH - 10, UI::COL_DIM);
        }
    }

    // Percentage on the right (or "—" if stale)
    c.setTextDatum(top_right);
    c.setFont(&fonts::Font4);
    if (b.is_stale) {
        c.setTextColor(UI::COL_DIM, UI::COL_BG);
        c.drawString("--", x + w - 6, b.y0 - 2);
    } else {
        c.setTextColor(UI::COL_FG, UI::COL_BG);
        char pct_buf[8];
        snprintf(pct_buf, sizeof(pct_buf), "%d%%", (int)(b.pct + 0.5f));
        c.drawString(pct_buf, x + w - 6, b.y0 - 2);
    }

    // Reset time
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(UI::COL_DIM, UI::COL_BG);
    c.setTextDatum(top_left);
    char reset_buf[20];
    if (b.is_weekly) {
        // Mon Jun 11 — fake by month/day from epoch
        if (b.resets_at == 0) {
            snprintf(reset_buf, sizeof(reset_buf), "reset ?");
        } else {
            // Approximate calendar from epoch — good enough for "look once"
            // (no leap-year-aware lib, fine for years 2026-2030)
            uint32_t local_days = (b.resets_at + 8 * 3600) / 86400;
            // Days since 1970-01-01 (Thursday)
            int y = 1970, days = local_days;
            while (true) {
                int leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
                int yd = leap ? 366 : 365;
                if (days < yd) break;
                days -= yd;
                y++;
            }
            int leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
            int dm[] = { 31, leap?29:28, 31,30,31,30,31,31,30,31,30,31 };
            int m = 0;
            while (days >= dm[m]) { days -= dm[m]; m++; }
            snprintf(reset_buf, sizeof(reset_buf), "reset %d/%d", m + 1, days + 1);
        }
    } else {
        char hhmm[8];
        formatHHMM(hhmm, sizeof(hhmm), b.resets_at);
        snprintf(reset_buf, sizeof(reset_buf), "reset %s", hhmm);
    }
    c.drawString(reset_buf, barX, barY + barH + 4);
}

void MonitorMode::renderPanel(M5Canvas& c, int x, int y, int w, int h) {
    c.fillRect(x, y, w, h, UI::COL_BG);

    // ── WiFi banner if not connected ──
    if (wifi_state_ != WF_READY) {
        c.setFont(&fonts::Font4);
        c.setTextColor(UI::COL_COOL, UI::COL_BG);
        c.setTextDatum(middle_center);
        const char* msg = (wifi_state_ == WF_CONNECTING) ? "WIFI..." :
                          (wifi_state_ == WF_FAILED) ? "NO WIFI" : "BOOT";
        c.drawString(msg, x + w/2, y + h/2 - 12);
        c.setFont(&fonts::Font0);
        c.setTextColor(UI::COL_DIM, UI::COL_BG);
        c.drawString(WiFiConfig::SSID, x + w/2, y + h/2 + 10);
        return;
    }

    const PageState& p = pages_[sub_];

    if (!p.have_data) {
        c.setFont(&fonts::Font4);
        c.setTextColor(UI::COL_COOL, UI::COL_BG);
        c.setTextDatum(middle_center);
        c.drawString(p.last_fetch_ok ? "FETCHING" : "OFFLINE", x + w/2, y + h/2 - 8);
        if (!p.last_fetch_ok && p.err_msg[0]) {
            c.setFont(&fonts::Font0);
            c.setTextColor(UI::COL_DIM, UI::COL_BG);
            c.drawString(p.err_msg, x + w/2, y + h/2 + 16);
        }
        return;
    }

    // ── Two stacked bars: 5 HOURS / WEEK ──
    bool primary_stale   = p.primary_rolled   || p.stale_seconds > STALE_THRESHOLD_S;
    bool secondary_stale = p.secondary_rolled || p.stale_seconds > STALE_THRESHOLD_S;

    BlockSpec a{};
    a.y0 = y + 4;
    a.label = "5 HOURS";
    a.pct = p.primary_pct;
    a.is_stale = primary_stale;
    a.rolled = p.primary_rolled;
    a.resets_at = p.primary_resets;
    a.is_weekly = false;
    drawBlock(c, x, w, a);

    BlockSpec b{};
    b.y0 = y + 76;
    b.label = "THIS WEEK";
    b.pct = p.secondary_pct;
    b.is_stale = secondary_stale;
    b.rolled = p.secondary_rolled;
    b.resets_at = p.secondary_resets;
    b.is_weekly = true;
    drawBlock(c, x, w, b);

    // ── Footer status ──
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextDatum(top_left);
    int footY = y + h - 14;

    if (primary_stale || secondary_stale) {
        c.setTextColor(UI::COL_DIM, UI::COL_BG);
        char age_buf[8];
        formatAge(age_buf, sizeof(age_buf), p.stale_seconds);
        char msg[28];
        snprintf(msg, sizeof(msg), "no codex use for %s", age_buf);
        c.drawString(msg, x + 6, footY);
    } else {
        c.setTextColor(UI::COL_OK, UI::COL_BG);
        c.drawString("live", x + 6, footY);
        c.fillCircle(x + 6 + 26, footY + 5, 2, UI::COL_OK);
    }

    c.setTextColor(UI::COL_DIM, UI::COL_BG);
    c.setTextDatum(top_right);
    c.drawString("A:refresh", x + w - 6, footY);
}
