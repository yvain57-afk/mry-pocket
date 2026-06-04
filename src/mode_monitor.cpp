#include "mode_monitor.h"
#include "ui.h"
#include "audio.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

using WiFiConfig::ENDPOINTS;
using WiFiConfig::ENDPOINT_COUNT;
using WiFiConfig::POLL_INTERVAL_MS;

void MonitorMode::enter() {
    startWifiIfNeeded();
}

void MonitorMode::exit() {
    // Keep WiFi alive — re-connecting is slow and other modes don't care.
}

const char* MonitorMode::subName() const {
    return ENDPOINTS[sub_].label;
}

void MonitorMode::cycleSubMode() {
    sub_ = (sub_ + 1) % ENDPOINT_COUNT;
    Audio::beep(1400, 30);
    // Force immediate refresh on the new page
    pages_[sub_].last_fetch_ms = 0;
}

void MonitorMode::onEvent(BtnEvent e) {
    if (e == EV_A_SHORT) {
        // Force refetch now
        pages_[sub_].last_fetch_ms = 0;
        Audio::beep(1800, 30);
    } else if (e == EV_A_LONG) {
        // also swap page on long-A (extra convenience — same as B long)
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
    // ── WiFi state machine ──
    switch (wifi_state_) {
        case WF_BOOT:
        case WF_FAILED:
            startWifiIfNeeded();
            break;
        case WF_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                wifi_state_ = WF_READY;
            } else if (now_ms - wifi_attempt_started_ms_ > WiFiConfig::WIFI_CONNECT_TIMEOUT_MS) {
                wifi_state_ = WF_FAILED;
            }
            break;
        case WF_READY:
            if (WiFi.status() != WL_CONNECTED) {
                wifi_state_ = WF_CONNECTING;
                wifi_attempt_started_ms_ = now_ms;
            }
            break;
    }
    if (wifi_state_ != WF_READY) return;

    // ── Fetch active page if it's due ──
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

    // Parse only the fields we need to keep RAM low
    StaticJsonDocument<512> filter;
    filter["data"]["primary"]["used_percent"]   = true;
    filter["data"]["primary"]["resets_at"]      = true;
    filter["data"]["secondary"]["used_percent"] = true;

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

    p.primary_pct     = data["primary"]["used_percent"]   | 0.0f;
    p.secondary_pct   = data["secondary"]["used_percent"] | 0.0f;
    p.resets_at_epoch = (uint32_t)(data["primary"]["resets_at"] | 0UL);
    p.have_data       = true;
    p.last_fetch_ok   = true;
    p.err_msg[0]      = 0;
}

// ── Ring progress drawing helper ──
// Draws an arc from 12 o'clock clockwise. Background ring in dim, foreground
// fill proportional to pct (0..100).
void MonitorMode::renderRing(M5Canvas& c, int cx, int cy, int r,
                             float pct, uint16_t fg_col) const {
    const int thick = 8;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    // Background full ring
    c.fillArc(cx, cy, r, r - thick, 0, 360, UI::COL_PANEL);
    // Foreground filled arc — start at 270° (top), sweep clockwise
    float sweep = pct * 360.0f / 100.0f;
    if (sweep > 0.5f) {
        c.fillArc(cx, cy, r, r - thick, 270, 270 + sweep, fg_col);
    }
}

void MonitorMode::renderPanel(M5Canvas& c, int x, int y, int w, int h) {
    c.fillRect(x, y, w, h, UI::COL_BG);
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(UI::COL_FG, UI::COL_BG);
    c.setTextDatum(top_left);

    const auto& ep = ENDPOINTS[sub_];
    c.drawString("CODEX 5h", x + 6, y + 6);
    c.setTextColor(UI::COL_COOL, UI::COL_BG);
    c.setTextDatum(top_right);
    c.drawString(ep.label, x + w - 6, y + 6);

    // WiFi banner if not connected
    if (wifi_state_ != WF_READY) {
        c.setFont(&fonts::Font4);
        c.setTextColor(UI::COL_ACCENT, UI::COL_BG);
        c.setTextDatum(middle_center);
        const char* msg = (wifi_state_ == WF_CONNECTING) ? "WIFI..." :
                          (wifi_state_ == WF_FAILED) ? "NO WIFI" : "BOOT";
        c.drawString(msg, x + w/2, y + h/2);
        c.setFont(&fonts::Font0);
        c.setTextColor(UI::COL_DIM, UI::COL_BG);
        c.drawString(WiFiConfig::SSID, x + w/2, y + h/2 + 22);
        return;
    }

    const PageState& p = pages_[sub_];

    if (!p.have_data) {
        c.setFont(&fonts::Font4);
        c.setTextColor(p.last_fetch_ok ? UI::COL_DIM : UI::COL_HOT, UI::COL_BG);
        c.setTextDatum(middle_center);
        c.drawString(p.last_fetch_ok ? "FETCHING" : "ERROR", x + w/2, y + h/2 - 10);
        if (!p.last_fetch_ok && p.err_msg[0]) {
            c.setFont(&fonts::Font0);
            c.setTextColor(UI::COL_DIM, UI::COL_BG);
            c.drawString(p.err_msg, x + w/2, y + h/2 + 18);
        }
        return;
    }

    // ── Big ring: primary 5h usage ──
    int cx = x + w / 2;
    int cy = y + 80;
    int r  = 50;
    uint16_t ring_col = (p.primary_pct >= 90) ? UI::COL_HOT :
                        (p.primary_pct >= 70) ? UI::COL_ACCENT :
                                                UI::COL_OK;
    renderRing(c, cx, cy, r, p.primary_pct, ring_col);

    // Center percentage text
    char pct_buf[8];
    snprintf(pct_buf, sizeof(pct_buf), "%d%%", (int)(p.primary_pct + 0.5f));
    c.setFont(&fonts::Font7);
    c.setTextColor(ring_col, UI::COL_BG);
    c.setTextDatum(middle_center);
    c.drawString(pct_buf, cx, cy);

    // ── Reset countdown ──
    // resets_at_epoch is UTC seconds. We have no RTC, so we trust the
    // server-provided value relative to its `fetched_at`. Simpler:
    // re-fetch /codex/usage already gives fresh resets_at; we just
    // display HH:MM until reset using current epoch from response… but
    // we didn't parse fetched_at into epoch yet. Compromise: show
    // resets_at as wall-clock HH:MM in Asia/Shanghai (UTC+8).
    if (p.resets_at_epoch > 0) {
        uint32_t local = p.resets_at_epoch + 8 * 3600;
        uint32_t hour = (local / 3600) % 24;
        uint32_t min  = (local / 60)   % 60;
        char buf[24];
        snprintf(buf, sizeof(buf), "resets %02lu:%02lu",
                 (unsigned long)hour, (unsigned long)min);
        c.setFont(&fonts::Font2);
        c.setTextColor(UI::COL_FG, UI::COL_BG);
        c.setTextDatum(top_center);
        c.drawString(buf, cx, cy + r + 14);
    }

    // ── Secondary (weekly) thin bar ──
    int sbY = y + h - 32;
    int sbW = w - 32;
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(UI::COL_DIM, UI::COL_BG);
    char sbLabel[24];
    snprintf(sbLabel, sizeof(sbLabel), "week %d%%", (int)(p.secondary_pct + 0.5f));
    c.drawString(sbLabel, x + 16, sbY - 10);

    c.drawRect(x + 16, sbY, sbW, 6, UI::COL_DIM);
    int sbFill = (int)(p.secondary_pct * (sbW - 2) / 100.0f);
    c.fillRect(x + 17, sbY + 1, sbFill, 4, UI::COL_COOL);

    // age of data
    uint32_t age_s = (millis() - p.last_fetch_ms) / 1000;
    char age_buf[20];
    snprintf(age_buf, sizeof(age_buf), "%lus ago  A:refresh", (unsigned long)age_s);
    c.setTextColor(UI::COL_DIM, UI::COL_BG);
    c.setTextDatum(top_right);
    c.drawString(age_buf, x + w - 8, y + h - 12);
}
