/*
 * --------------------------------------------------------------------
 * config.h - hardware pins, layout geometry, palettes, timings
 * --------------------------------------------------------------------
 * Display and touch pins are NOT here - they live in platformio.ini
 * build_flags because TFT_eSPI needs them at library compile time.
 * --------------------------------------------------------------------
 */

#pragma once

#include <Arduino.h>

/* --------------------------------------------------------------------
 * SCREEN
 * -------------------------------------------------------------------- */
constexpr int16_t SCREEN_W = 480;
constexpr int16_t SCREEN_H = 320;
constexpr uint8_t SCREEN_ROTATION = 3; // landscape, USB to the left

/* --------------------------------------------------------------------
 * BACKLIGHT
 * --------------------------------------------------------------------
 * Optional. To enable dimming, move the display's LED wire from 3V3 to
 * GPIO 32 (same J1 header, five pins down). Leave it on 3V3 and set
 * BACKLIGHT_ENABLED to 0 - night mode still re-themes the UI.
 * -------------------------------------------------------------------- */
#define BACKLIGHT_ENABLED 1
constexpr uint8_t BACKLIGHT_PIN = 32;
constexpr uint8_t BACKLIGHT_CHANNEL = 0;
constexpr uint32_t BACKLIGHT_FREQ_HZ = 5000;
constexpr uint8_t BACKLIGHT_BITS = 8;
constexpr uint8_t BACKLIGHT_DAY = 255;
constexpr uint8_t BACKLIGHT_NIGHT = 45;

/* --------------------------------------------------------------------
 * LOCATION AND TIME
 * -------------------------------------------------------------------- */
constexpr float DEFAULT_LAT = 12.9716f; // Bangalore, used until IP lookup
constexpr float DEFAULT_LON = 77.5946f;
constexpr long GMT_OFFSET_SEC = 19800; // IST, UTC+5:30
constexpr int DST_OFFSET_SEC = 0;

/* --------------------------------------------------------------------
 * NIGHT MODE
 * --------------------------------------------------------------------
 * Auto-switches at these hours. A tap on the clock overrides until the
 * next boundary is crossed.
 * -------------------------------------------------------------------- */
#define NIGHT_AUTO_ENABLED 1
constexpr int NIGHT_START_HOUR = 22; // 22:00
constexpr int NIGHT_END_HOUR = 6;    // 06:00

/* --------------------------------------------------------------------
 * TIMINGS (ms)
 * -------------------------------------------------------------------- */
constexpr uint32_t WEATHER_REFRESH_MS = 15UL * 60UL * 1000UL; // Open-Meteo updates ~15 min
constexpr uint32_t WEATHER_RETRY_MS = 60UL * 1000UL;          // backoff after a failure
constexpr uint32_t WIFI_RETRY_MS = 30UL * 1000UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20UL * 1000UL;
constexpr uint32_t NTP_RESYNC_MS = 6UL * 60UL * 60UL * 1000UL; // drift correction
constexpr uint32_t NTP_RETRY_MS = 60UL * 1000UL;               // while still unsynced
constexpr uint32_t TOUCH_DEBOUNCE_MS = 400UL;
// Raw XPT2046 pressure that counts as "a finger is down". Used as a cheap
// gate before the much more expensive getTouch(), and to detect a press
// during boot before any calibration has been applied. Kept below
// TFT_eSPI's own getTouch() threshold of 600 so it never gates out a
// press that getTouch() would have accepted.
constexpr uint16_t TOUCH_Z_THRESHOLD = 400;
constexpr uint32_t TOAST_DURATION_MS = 1600UL;

/* --------------------------------------------------------------------
 * LAYOUT
 * --------------------------------------------------------------------
 * Split dashboard: clock fills the left, weather column on the right,
 * one hairline rule between them.
 * -------------------------------------------------------------------- */
constexpr int16_t DIVIDER_X = 306;
constexpr int16_t DIVIDER_TOP = 34;
constexpr int16_t DIVIDER_BOTTOM = 286;

// Left panel - clock.
// "12:35" in FreeSansBold24pt7b at textsize 2 measures 232 x 70, so a
// 290 x 78 sprite holds it plus the AM/PM label with margin.
constexpr int16_t CLOCK_CX = DIVIDER_X / 2; // 153
constexpr int16_t TIME_SPRITE_X = 8;
constexpr int16_t TIME_SPRITE_Y = 100;
constexpr int16_t TIME_SPRITE_W = 290;
constexpr int16_t TIME_SPRITE_H = 78;
constexpr int16_t ACCENT_RULE_Y = 194;
constexpr int16_t ACCENT_RULE_W = 54;
constexpr int16_t DATE_Y = 224;

// Right panel - weather
constexpr int16_t WX_CX = DIVIDER_X + (SCREEN_W - DIVIDER_X) / 2; // 393
constexpr int16_t WIFI_ICON_X = SCREEN_W - 30;
constexpr int16_t WIFI_ICON_Y = 28;
constexpr int16_t WX_ICON_CY = 92;
constexpr int16_t TEMP_SPRITE_X = DIVIDER_X + 8;
constexpr int16_t TEMP_SPRITE_Y = 140;
constexpr int16_t TEMP_SPRITE_W = SCREEN_W - DIVIDER_X - 16; // 158
constexpr int16_t TEMP_SPRITE_H = 44;
constexpr int16_t COND_Y = 198;
constexpr int16_t WX_RULE_Y = 218;
constexpr int16_t HILO_Y = 240;
// Toast band sits fully below DIVIDER_BOTTOM (286) so clearing it never
// erases the bottom of the divider.
constexpr int16_t TOAST_Y = 302;
constexpr int16_t TOAST_H = 24;

/* --------------------------------------------------------------------
 * PALETTE (RGB565)
 * -------------------------------------------------------------------- */
struct Theme
{
    uint16_t bg;
    uint16_t textPrimary;
    uint16_t textSecondary;
    uint16_t accent;   // temperature, weather emphasis
    uint16_t divider;
    uint16_t iconWarm; // sun
    uint16_t iconCool; // cloud
    uint16_t ok;
    uint16_t warn;
    uint16_t error;
};

// Day - near-black slate ground, white clock, amber weather
constexpr Theme THEME_DAY = {
    0x10A3, // bg          #0E141B
    0xFFFF, // textPrimary #FFFFFF
    0x7C53, // textSec     #7C8B9C
    0xFD88, // accent      #FFB443
    0x2147, // divider     #1E2A38
    0xFD88, // iconWarm    #FFB443
    0xC618, // iconCool    #C0C0C0
    0x4EF0, // ok          #4ADE80
    0xFDE4, // warn        #FBBF24
    0xF38E  // error       #F87171
};

// Night - dim amber on true black, easy on dark-adapted eyes
constexpr Theme THEME_NIGHT = {
    0x0000, // bg
    0x8AA4, // textPrimary #8A5520
    0x4962, // textSec     #4A2E12
    0x8AA4, // accent
    0x20C1, // divider     #241708
    0x8AA4, // iconWarm
    0x4962, // iconCool
    0x4962, // ok
    0x8AA4, // warn
    0x8AA4  // error
};
