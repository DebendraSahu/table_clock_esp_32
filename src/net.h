/*
 * --------------------------------------------------------------------
 * net.h - WiFi, NTP and Open-Meteo weather
 * --------------------------------------------------------------------
 * Everything here is non-blocking apart from the weather HTTPS request,
 * which runs at most once a minute and only when due.
 * --------------------------------------------------------------------
 */

#pragma once

#include <Arduino.h>

// Set to 0 to always use DEFAULT_LAT/DEFAULT_LON from config.h
#define LOCATION_FROM_IP 1

// Anything below this epoch means the clock has never been set
constexpr time_t TIME_VALID_EPOCH = 1700000000; // 2023-11-14

/* --------------------------------------------------------------------
 * DATA
 * -------------------------------------------------------------------- */
struct WeatherData
{
    float temp = NAN;
    float tempMax = NAN;
    float tempMin = NAN;
    int code = -1;
    bool isDay = true;
    const char *condition = "--";
    bool valid = false;
};

struct ClockData
{
    int hour24 = 0;
    int hour12 = 12;
    int minute = 0;
    int second = 0;
    bool isPM = false;
    int day = 1;
    int month = 1;
    int year = 2026;
    const char *dayName = "---";
    const char *monthName = "---";
    bool synced = false;
};

/* --------------------------------------------------------------------
 * WHAT CHANGED SINCE THE LAST net::update() CALL
 * -------------------------------------------------------------------- */
struct NetEvents
{
    bool minuteChanged = false;
    bool weatherChanged = false;
    bool wifiChanged = false;
    bool timeJustSynced = false;
};

namespace net
{
    void begin();

    // Call every loop. Drives reconnects, NTP re-sync and scheduled
    // weather refreshes, and reports what the UI needs to repaint.
    NetEvents update();

    // Touch-triggered refresh. Returns false if offline.
    bool refreshWeatherNow();

    bool isConnected();
    int rssi();
    String ipAddress();

    const WeatherData &weather();
    const ClockData &clock();

    // True while a weather request is in flight (for the UI indicator)
    bool isFetching();
}
