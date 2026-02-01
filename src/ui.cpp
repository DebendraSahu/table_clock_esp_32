/*
 * --------------------------------------------------------------------
 * ui.cpp - split-dashboard rendering
 * --------------------------------------------------------------------
 * Layout:
 *
 *   +---------------------------+-------------+
 *   |                           |    icon     |
 *   |         10:42 PM          |    27 C     |
 *   |         ------            |   CLEAR     |
 *   |      MON 19 JAN 2026      |  H31 / L22  |
 *   +---------------------------+-------------+
 *
 * TFT_eSprite derives from TFT_eSPI, so every render function takes a
 * TFT_eSPI& and works unchanged whether it targets a sprite or the
 * panel directly. That gives a clean fallback if a sprite allocation
 * ever fails.
 * --------------------------------------------------------------------
 */

#include "ui.h"
#include "config.h"

// Anti-aliased 72px clock face. The GFX free fonts below are 1-bit, so
// scaling them up blocks up every edge; this one carries 8-bit coverage
// per pixel and blends against the background instead.
#include "font_clock72.h"

// The GFX free fonts (FreeSansBold24pt7b etc.) arrive via TFT_eSPI.h ->
// Fonts/GFXFF/gfxfont.h, which includes them all. Including the font
// headers again here is a redefinition error.

namespace
{
    TFT_eSPI g_tft = TFT_eSPI();

    TFT_eSprite g_timeSpr = TFT_eSprite(&g_tft);
    TFT_eSprite g_tempSpr = TFT_eSprite(&g_tft);
    bool g_timeSprOk = false;
    bool g_tempSprOk = false;

    Theme g_theme = THEME_DAY;
    bool g_night = false;

    uint32_t g_toastUntil = 0;
    bool g_toastVisible = false;

    /* ================================================================
     * SMALL HELPERS
     * ================================================================ */

    // GFX free fonts are ASCII-only (0x20-0x7E) so there is no degree
    // glyph. Draw it as a ring instead.
    void drawDegreeRing(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t color)
    {
        g.drawCircle(cx, cy, r, color);
        g.drawCircle(cx, cy, r - 1, color);
    }

    /* ================================================================
     * WEATHER ICONS
     * ================================================================ */

    void drawSun(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t color)
    {
        for (int i = 0; i < 8; i++)
        {
            const float a = i * 45.0f * DEG_TO_RAD;
            const int16_t x1 = cx + cos(a) * (r + 6);
            const int16_t y1 = cy + sin(a) * (r + 6);
            const int16_t x2 = cx + cos(a) * (r + 13);
            const int16_t y2 = cy + sin(a) * (r + 13);

            g.drawLine(x1, y1, x2, y2, color);
            g.drawLine(x1 + 1, y1, x2 + 1, y2, color);
        }
        g.fillCircle(cx, cy, r, color);
    }

    void drawMoon(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t r, uint16_t color, uint16_t bg)
    {
        g.fillCircle(cx, cy, r, color);
        g.fillCircle(cx + r * 0.55f, cy - r * 0.42f, r * 0.92f, bg);
    }

    void drawCloud(TFT_eSPI &g, int16_t cx, int16_t cy, int16_t w, uint16_t color)
    {
        const float s = w / 60.0f;
        g.fillCircle(cx - 16 * s, cy + 2 * s, 11 * s, color);
        g.fillCircle(cx + 2 * s, cy - 8 * s, 16 * s, color);
        g.fillCircle(cx + 18 * s, cy + 2 * s, 12 * s, color);
        g.fillRoundRect(cx - 27 * s, cy + 2 * s, 54 * s, 13 * s, 6 * s, color);
    }

    void drawRainDrops(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t color)
    {
        for (int i = -1; i <= 1; i++)
        {
            const int16_t x = cx + i * 14;
            g.drawLine(x + 3, cy, x - 3, cy + 12, color);
            g.drawLine(x + 4, cy, x - 2, cy + 12, color);
        }
    }

    void drawSnowFlakes(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t color)
    {
        for (int i = -1; i <= 1; i++)
        {
            const int16_t x = cx + i * 14;
            const int16_t y = cy + 6;
            g.drawLine(x - 4, y, x + 4, y, color);
            g.drawLine(x, y - 4, x, y + 4, color);
            g.drawLine(x - 3, y - 3, x + 3, y + 3, color);
            g.drawLine(x - 3, y + 3, x + 3, y - 3, color);
        }
    }

    void drawBolt(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t color)
    {
        g.fillTriangle(cx + 4, cy - 2, cx - 8, cy + 14, cx + 1, cy + 14, color);
        g.fillTriangle(cx + 8, cy - 2, cx + 1, cy + 14, cx + 10, cy + 2, color);
    }

    void drawFogBars(TFT_eSPI &g, int16_t cx, int16_t cy, uint16_t color)
    {
        for (int i = 0; i < 4; i++)
        {
            const int16_t y = cy - 18 + i * 12;
            const int16_t w = (i % 2 == 0) ? 54 : 42;
            g.fillRoundRect(cx - w / 2, y, w, 6, 3, color);
        }
    }

    void drawWeatherIcon(TFT_eSPI &g, int code, bool isDay, int16_t cx, int16_t cy)
    {
        const uint16_t warm = g_theme.iconWarm;
        const uint16_t cool = g_theme.iconCool;
        const uint16_t bg = g_theme.bg;

        switch (code)
        {
        case 0: // clear
            if (isDay) drawSun(g, cx, cy, 22, warm);
            else       drawMoon(g, cx, cy, 22, cool, bg);
            break;

        case 1: // mostly clear
        case 2: // partly cloudy
            if (isDay) drawSun(g, cx + 14, cy - 16, 14, warm);
            else       drawMoon(g, cx + 14, cy - 16, 14, cool, bg);
            drawCloud(g, cx - 4, cy + 10, 56, cool);
            break;

        case 3: // overcast
            drawCloud(g, cx - 6, cy - 6, 46, cool);
            drawCloud(g, cx + 4, cy + 10, 58, warm);
            break;

        case 45:
        case 48:
            drawFogBars(g, cx, cy, cool);
            break;

        case 51: case 53: case 55:
        case 56: case 57:
        case 61: case 63: case 65:
        case 66: case 67:
        case 80: case 81: case 82:
            drawCloud(g, cx, cy - 10, 56, cool);
            drawRainDrops(g, cx, cy + 14, warm);
            break;

        case 71: case 73: case 75: case 77:
        case 85: case 86:
            drawCloud(g, cx, cy - 10, 56, cool);
            drawSnowFlakes(g, cx, cy + 10, warm);
            break;

        case 95: case 96: case 99:
            drawCloud(g, cx, cy - 10, 56, cool);
            drawBolt(g, cx, cy + 10, warm);
            break;

        default:
            drawCloud(g, cx, cy, 56, cool);
            break;
        }
    }

    /* ================================================================
     * REGION RENDERERS
     * ----------------------------------------------------------------
     * ox/oy let the same code target a sprite (0,0) or the panel.
     * ================================================================ */

    void renderTime(TFT_eSPI &g, int16_t ox, int16_t oy)
    {
        const ClockData &c = net::clock();

        char digits[8];
        if (c.synced)
        {
            snprintf(digits, sizeof(digits), "%d:%02d", c.hour12, c.minute);
        }
        else
        {
            snprintf(digits, sizeof(digits), "--:--");
        }

        const char *suffix = c.isPM ? "PM" : "AM";
        const int16_t midY = oy + TIME_SPRITE_H / 2;

        // Measure the small suffix first, while the free font is still
        // active - loading a VLW font overrides setFreeFont() entirely.
        g.setFreeFont(&FreeSansBold12pt7b);
        g.setTextSize(1);
        const int16_t wSuffix = c.synced ? g.textWidth(suffix) : 0;
        const int16_t gap = c.synced ? 12 : 0;

        // Digits in the smooth font. Every digit advances 40px, so the
        // block width only changes between 1- and 2-digit hours.
        g.loadFont(FontClock72);
        const int16_t wDigits = g.textWidth(digits);
        const int16_t left = ox + (TIME_SPRITE_W - (wDigits + gap + wSuffix)) / 2;

        g.setTextDatum(ML_DATUM);
        // A VLW glyph blends its edges toward textbgcolor, so the second
        // argument is required or the anti-aliasing fringes the wrong way.
        g.setTextColor(c.synced ? g_theme.textPrimary : g_theme.textSecondary,
                       g_theme.bg);
        g.drawString(digits, left, midY);
        g.unloadFont();

        if (c.synced)
        {
            g.setFreeFont(&FreeSansBold12pt7b);
            g.setTextSize(1);
            g.setTextDatum(ML_DATUM);
            g.setTextColor(g_theme.accent, g_theme.bg);
            // Sit the suffix on the digits' cap line rather than centre
            g.drawString(suffix, left + wDigits + gap, midY - 20);
        }
    }

    void renderTemp(TFT_eSPI &g, int16_t ox, int16_t oy)
    {
        const WeatherData &w = net::weather();
        const int16_t midY = oy + TEMP_SPRITE_H / 2;

        char num[8];
        if (w.valid && !isnan(w.temp))
        {
            snprintf(num, sizeof(num), "%.0f", w.temp);
        }
        else
        {
            snprintf(num, sizeof(num), "--");
        }

        g.setFreeFont(&FreeSansBold18pt7b);
        g.setTextSize(1);

        const int16_t wNum = g.textWidth(num);
        const int16_t wUnit = g.textWidth("C");
        const int16_t ringR = 5;
        const int16_t total = wNum + 4 + ringR * 2 + 3 + wUnit;
        const int16_t left = ox + (TEMP_SPRITE_W - total) / 2;

        g.setTextDatum(ML_DATUM);
        g.setTextColor(g_theme.accent);
        g.drawString(num, left, midY);

        drawDegreeRing(g, left + wNum + 4 + ringR, midY - 10, ringR, g_theme.accent);
        g.drawString("C", left + wNum + 4 + ringR * 2 + 3, midY);
    }
}

/* ====================================================================
 * PUBLIC
 * ==================================================================== */
namespace ui
{
    TFT_eSPI &display() { return g_tft; }

    void begin()
    {
        g_tft.init();
        g_tft.setRotation(SCREEN_ROTATION);
        g_tft.fillScreen(g_theme.bg);

        g_timeSpr.setColorDepth(16);
        g_timeSprOk = (g_timeSpr.createSprite(TIME_SPRITE_W, TIME_SPRITE_H) != nullptr);

        g_tempSpr.setColorDepth(16);
        g_tempSprOk = (g_tempSpr.createSprite(TEMP_SPRITE_W, TEMP_SPRITE_H) != nullptr);

        Serial.printf("[UI] sprites: time=%s temp=%s\n",
                      g_timeSprOk ? "ok" : "FALLBACK",
                      g_tempSprOk ? "ok" : "FALLBACK");
    }

    void setNight(bool on)
    {
        if (on == g_night)
        {
            return;
        }
        g_night = on;
        g_theme = on ? THEME_NIGHT : THEME_DAY;
        drawAll();
    }

    bool isNight() { return g_night; }

    uint16_t colorOk() { return g_theme.ok; }
    uint16_t colorWarn() { return g_theme.warn; }
    uint16_t colorError() { return g_theme.error; }

    /* ---------------- regions ---------------- */

    void drawTime()
    {
        if (g_timeSprOk)
        {
            g_timeSpr.fillSprite(g_theme.bg);
            renderTime(g_timeSpr, 0, 0);
            g_timeSpr.pushSprite(TIME_SPRITE_X, TIME_SPRITE_Y);
        }
        else
        {
            g_tft.fillRect(TIME_SPRITE_X, TIME_SPRITE_Y,
                           TIME_SPRITE_W, TIME_SPRITE_H, g_theme.bg);
            renderTime(g_tft, TIME_SPRITE_X, TIME_SPRITE_Y);
        }
    }

    void drawDate()
    {
        const ClockData &c = net::clock();

        char buf[24];
        if (c.synced)
        {
            snprintf(buf, sizeof(buf), "%s %d %s %d",
                     c.dayName, c.day, c.monthName, c.year);
        }
        else
        {
            snprintf(buf, sizeof(buf), "WAITING FOR TIME");
        }

        // Accent rule above the date
        g_tft.fillRect(CLOCK_CX - ACCENT_RULE_W / 2, ACCENT_RULE_Y,
                       ACCENT_RULE_W, 3, g_theme.accent);

        g_tft.fillRect(0, DATE_Y - 16, DIVIDER_X - 2, 32, g_theme.bg);
        g_tft.setFreeFont(&FreeSansBold12pt7b);
        g_tft.setTextSize(1);
        g_tft.setTextDatum(MC_DATUM);
        g_tft.setTextColor(g_theme.textSecondary);
        g_tft.drawString(buf, CLOCK_CX, DATE_Y);
    }

    void drawWeather()
    {
        const WeatherData &w = net::weather();
        const int16_t panelX = DIVIDER_X + 2;
        const int16_t panelW = SCREEN_W - panelX;

        // Icon area
        g_tft.fillRect(panelX, 48, panelW, 84, g_theme.bg);
        drawWeatherIcon(g_tft, w.valid ? w.code : -1, w.isDay, WX_CX, WX_ICON_CY);

        // Temperature
        if (g_tempSprOk)
        {
            g_tempSpr.fillSprite(g_theme.bg);
            renderTemp(g_tempSpr, 0, 0);
            g_tempSpr.pushSprite(TEMP_SPRITE_X, TEMP_SPRITE_Y);
        }
        else
        {
            g_tft.fillRect(TEMP_SPRITE_X, TEMP_SPRITE_Y,
                           TEMP_SPRITE_W, TEMP_SPRITE_H, g_theme.bg);
            renderTemp(g_tft, TEMP_SPRITE_X, TEMP_SPRITE_Y);
        }

        // Condition
        g_tft.fillRect(panelX, COND_Y - 12, panelW, 24, g_theme.bg);
        g_tft.setFreeFont(&FreeSans9pt7b);
        g_tft.setTextSize(1);
        g_tft.setTextDatum(MC_DATUM);
        g_tft.setTextColor(g_theme.textPrimary);
        g_tft.drawString(w.valid ? w.condition : "NO DATA", WX_CX, COND_Y);

        // Hairline, then high / low
        g_tft.fillRect(WX_CX - 30, WX_RULE_Y, 60, 1, g_theme.divider);
        g_tft.fillRect(panelX, HILO_Y - 12, panelW, 24, g_theme.bg);

        if (w.valid && !isnan(w.tempMax) && !isnan(w.tempMin))
        {
            char hi[8], lo[8];
            snprintf(hi, sizeof(hi), "%.0f", w.tempMax);
            snprintf(lo, sizeof(lo), "%.0f", w.tempMin);

            g_tft.setFreeFont(&FreeSansBold9pt7b);
            g_tft.setTextDatum(ML_DATUM);

            const int16_t wH = g_tft.textWidth("H ") + g_tft.textWidth(hi);
            const int16_t wL = g_tft.textWidth("L ") + g_tft.textWidth(lo);
            const int16_t total = wH + 8 + 18 + wL;
            int16_t x = WX_CX - total / 2;

            g_tft.setTextColor(g_theme.textSecondary);
            g_tft.drawString("H ", x, HILO_Y);
            x += g_tft.textWidth("H ");
            g_tft.setTextColor(g_theme.textPrimary);
            g_tft.drawString(hi, x, HILO_Y);
            x += g_tft.textWidth(hi);
            drawDegreeRing(g_tft, x + 5, HILO_Y - 6, 3, g_theme.textPrimary);
            x += 18;

            g_tft.setTextColor(g_theme.textSecondary);
            g_tft.drawString("L ", x, HILO_Y);
            x += g_tft.textWidth("L ");
            g_tft.setTextColor(g_theme.textPrimary);
            g_tft.drawString(lo, x, HILO_Y);
            x += g_tft.textWidth(lo);
            drawDegreeRing(g_tft, x + 5, HILO_Y - 6, 3, g_theme.textPrimary);
        }
    }

    void drawWifi()
    {
        const int16_t x = WIFI_ICON_X;
        const int16_t y = WIFI_ICON_Y;
        const bool up = net::isConnected();

        g_tft.fillRect(x - 18, y - 14, 36, 26, g_theme.bg);

        if (!up)
        {
            // Hollow marker plus a slash when offline
            g_tft.drawCircle(x, y + 6, 3, g_theme.error);
            g_tft.drawLine(x - 11, y + 9, x + 11, y - 9, g_theme.error);
            return;
        }

        // Bar strength from RSSI
        const int r = net::rssi();
        int bars = 1;
        if (r > -60)      bars = 4;
        else if (r > -70) bars = 3;
        else if (r > -80) bars = 2;

        for (int i = 0; i < 4; i++)
        {
            const int16_t h = 4 + i * 4;
            const int16_t bx = x - 12 + i * 7;
            const uint16_t c = (i < bars) ? g_theme.ok : g_theme.divider;
            g_tft.fillRoundRect(bx, y + 8 - h, 5, h, 1, c);
        }
    }

    /* ---------------- composite ---------------- */

    void drawAll()
    {
        g_tft.fillScreen(g_theme.bg);

        g_tft.drawFastVLine(DIVIDER_X, DIVIDER_TOP,
                            DIVIDER_BOTTOM - DIVIDER_TOP, g_theme.divider);

        drawTime();
        drawDate();
        drawWeather();
        drawWifi();

        g_toastVisible = false;
    }

    /* ---------------- boot + toast ---------------- */

    void showBoot(const char *line1, const char *line2)
    {
        g_tft.fillScreen(g_theme.bg);

        g_tft.setFreeFont(&FreeSansBold18pt7b);
        g_tft.setTextSize(1);
        g_tft.setTextDatum(MC_DATUM);
        g_tft.setTextColor(g_theme.textPrimary);
        g_tft.drawString(line1, SCREEN_W / 2, SCREEN_H / 2 - 16);

        if (line2 && line2[0])
        {
            g_tft.setFreeFont(&FreeSans9pt7b);
            g_tft.setTextColor(g_theme.textSecondary);
            g_tft.drawString(line2, SCREEN_W / 2, SCREEN_H / 2 + 22);
        }
    }

    void showToast(const char *msg, uint16_t color)
    {
        g_tft.fillRect(0, TOAST_Y - TOAST_H / 2, SCREEN_W, TOAST_H, g_theme.bg);

        g_tft.setFreeFont(&FreeSansBold9pt7b);
        g_tft.setTextSize(1);
        g_tft.setTextDatum(MC_DATUM);
        g_tft.setTextColor(color);
        g_tft.drawString(msg, SCREEN_W / 2, TOAST_Y);

        g_toastUntil = millis() + TOAST_DURATION_MS;
        g_toastVisible = true;
    }

    void clearToastIfExpired()
    {
        if (!g_toastVisible || (int32_t)(millis() - g_toastUntil) < 0)
        {
            return;
        }
        g_tft.fillRect(0, TOAST_Y - TOAST_H / 2, SCREEN_W, TOAST_H, g_theme.bg);
        g_toastVisible = false;
    }
}
