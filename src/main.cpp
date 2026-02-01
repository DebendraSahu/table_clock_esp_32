/*
 * --------------------------------------------------------------------
 * TABLE CLOCK - ESP32 + 3.5" ILI9488 + XPT2046
 * --------------------------------------------------------------------
 * Split-dashboard clock: 12-hour time and date on the left, live
 * weather on the right.
 *
 *   Tap the LEFT half   -> toggle night mode (dim amber on black)
 *   Tap the RIGHT half  -> refresh weather now
 *   Hold at power-on    -> re-run touch calibration
 *
 * Display/touch pins and bus speeds live in platformio.ini build_flags.
 * Layout, palette and timings live in config.h.
 * WiFi credentials live in secrets.h, which is gitignored.
 * --------------------------------------------------------------------
 */

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"
#include "net.h"
#include "ui.h"

namespace
{
    Preferences g_prefs;

    int g_lastDay = -1;
    // A manual tap deliberately does NOT update g_lastAutoNight, so the
    // schedule leaves the override alone until the next boundary.
    bool g_lastAutoNight = false;

    uint32_t g_lastTouchMs = 0;
    uint32_t g_lastWifiIconMs = 0;
    bool g_touchDown = false; // true while a press is still being held

    /* ----------------------------------------------------------------
     * BACKLIGHT
     * ---------------------------------------------------------------- */
    void backlightBegin()
    {
#if BACKLIGHT_ENABLED
        ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ_HZ, BACKLIGHT_BITS);
        ledcAttachPin(BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
        ledcWrite(BACKLIGHT_CHANNEL, BACKLIGHT_DAY);
#endif
    }

    void backlightSet(bool night)
    {
#if BACKLIGHT_ENABLED
        ledcWrite(BACKLIGHT_CHANNEL, night ? BACKLIGHT_NIGHT : BACKLIGHT_DAY);
#else
        (void)night;
#endif
    }

    /* ----------------------------------------------------------------
     * TOUCH CALIBRATION - stored in NVS so it survives reflashing
     * ---------------------------------------------------------------- */
    void setupTouch()
    {
        TFT_eSPI &tft = ui::display();
        uint16_t cal[5] = {0};

        g_prefs.begin("clock", false);
        const size_t got = g_prefs.getBytes("touchcal", cal, sizeof(cal));

        // Holding the screen at power-on forces a fresh calibration.
        // This must read raw pressure, not getTouch(): no calibration has
        // been applied yet, so getTouch() would map a real press through
        // TFT_eSPI's placeholder values, land off-screen, and report false.
        const bool held = (tft.getTouchRawZ() > TOUCH_Z_THRESHOLD);

        if (got == sizeof(cal) && !held)
        {
            tft.setTouch(cal);
            Serial.println("[TOUCH] calibration loaded from NVS");
        }
        else
        {
            Serial.println("[TOUCH] calibrating - tap each corner arrow");
            ui::showBoot("Touch Setup", "Tap each corner arrow");
            delay(1200);

            tft.fillScreen(TFT_BLACK);
            tft.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 20);

            g_prefs.putBytes("touchcal", cal, sizeof(cal));
            tft.setTouch(cal);

            Serial.printf("[TOUCH] saved: %u %u %u %u %u\n",
                          cal[0], cal[1], cal[2], cal[3], cal[4]);
        }

        g_prefs.end();
    }

    /* ----------------------------------------------------------------
     * NIGHT MODE
     * ---------------------------------------------------------------- */
    bool autoNightFor(int hour)
    {
        if (NIGHT_START_HOUR > NIGHT_END_HOUR) // window wraps midnight
        {
            return (hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR);
        }
        return (hour >= NIGHT_START_HOUR && hour < NIGHT_END_HOUR);
    }

    void applyNight(bool night)
    {
        backlightSet(night);
        ui::setNight(night); // repaints only if the value actually changed
    }

    void updateNightSchedule()
    {
#if NIGHT_AUTO_ENABLED
        const ClockData &c = net::clock();
        if (!c.synced)
        {
            return;
        }

        const bool want = autoNightFor(c.hour24);
        if (want != g_lastAutoNight)
        {
            // A scheduled boundary was crossed - it wins over a manual tap
            g_lastAutoNight = want;
            applyNight(want);
        }
#endif
    }

    /* ----------------------------------------------------------------
     * TOUCH HANDLING
     * ---------------------------------------------------------------- */
    void handleTouch()
    {
        TFT_eSPI &tft = ui::display();

        // Cheap pressure gate first. getTouch() runs up to five validated
        // sample rounds with internal delays, which is pure wasted SPI
        // traffic on the vast majority of loops where nothing is touching.
        if (tft.getTouchRawZ() <= TOUCH_Z_THRESHOLD)
        {
            g_touchDown = false; // released - re-arm for the next press
            return;
        }

        // Act on the press EDGE only. Without this, resting a finger on the
        // right half re-fires every TOUCH_DEBOUNCE_MS and hammers the
        // weather API with blocking HTTPS requests.
        if (g_touchDown)
        {
            return;
        }

        const uint32_t now = millis();
        if (now - g_lastTouchMs < TOUCH_DEBOUNCE_MS)
        {
            return;
        }

        uint16_t x, y;
        if (!tft.getTouch(&x, &y))
        {
            return; // pressure but no stable coordinate - ignore this sample
        }

        g_touchDown = true;
        g_lastTouchMs = now;

        if (x < DIVIDER_X)
        {
            // Left half - flip the theme
            applyNight(!ui::isNight());
            ui::showToast(ui::isNight() ? "NIGHT MODE" : "DAY MODE", ui::colorOk());
        }
        else
        {
            // Right half - pull fresh weather
            if (!net::isConnected())
            {
                ui::showToast("OFFLINE", ui::colorError());
                return;
            }

            ui::showToast("UPDATING...", ui::colorWarn());
            const bool ok = net::refreshWeatherNow();

            if (ok)
            {
                ui::drawWeather();
                ui::showToast("WEATHER UPDATED", ui::colorOk());
            }
            else
            {
                ui::showToast("UPDATE FAILED", ui::colorError());
            }
        }
    }
}

/* --------------------------------------------------------------------
 * SETUP
 * -------------------------------------------------------------------- */
void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("\n========================================");
    Serial.println("  TABLE CLOCK");
    Serial.println("========================================\n");

    backlightBegin();
    ui::begin();

    ui::showBoot("Table Clock", "starting up");
    delay(600);

    setupTouch();

    ui::showBoot("Table Clock", "connecting to WiFi");
    net::begin();

    // Paint the real UI immediately - it fills in as data arrives
    ui::drawAll();

    const ClockData &c = net::clock();
    g_lastDay = c.day;
    g_lastAutoNight = autoNightFor(c.hour24);

    Serial.println("[MAIN] running\n");
}

/* --------------------------------------------------------------------
 * LOOP
 * -------------------------------------------------------------------- */
void loop()
{
    const NetEvents ev = net::update();

    if (ev.timeJustSynced)
    {
        // First real time - repaint everything at once
        ui::drawAll();
        g_lastDay = net::clock().day;
    }
    else
    {
        if (ev.minuteChanged)
        {
            ui::drawTime();

            const int day = net::clock().day;
            if (day != g_lastDay)
            {
                g_lastDay = day;
                ui::drawDate();
            }
        }

        if (ev.weatherChanged)
        {
            ui::drawWeather();
        }
    }

    if (ev.wifiChanged)
    {
        ui::drawWifi();
    }

    // Refresh the signal bars periodically even when the link is stable
    const uint32_t now = millis();
    if (now - g_lastWifiIconMs >= 30000UL)
    {
        g_lastWifiIconMs = now;
        ui::drawWifi();
    }

    updateNightSchedule();
    handleTouch();
    ui::clearToastIfExpired();

    delay(20);
}
