/*
 * --------------------------------------------------------------------
 * net.cpp - WiFi, NTP and Open-Meteo weather
 * --------------------------------------------------------------------
 */

#include "net.h"
#include "config.h"
#include "secrets.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

namespace
{
    WeatherData g_weather;
    ClockData g_clock;

    float g_lat = DEFAULT_LAT;
    float g_lon = DEFAULT_LON;
    bool g_locationResolved = false;

    bool g_wasConnected = false;
    bool g_ntpConfigured = false;
    bool g_fetching = false;

    uint32_t g_lastWifiAttempt = 0;
    uint32_t g_lastNtpAttempt = 0;
    uint32_t g_lastNtpSuccess = 0;
    uint32_t g_nextWeatherDue = 0;

    int g_lastMinute = -1;

    const char *const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                  "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const char *const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

    /* ----------------------------------------------------------------
     * WMO weather code -> short label
     * ---------------------------------------------------------------- */
    const char *describeCode(int code)
    {
        switch (code)
        {
        case 0:  return "CLEAR";
        case 1:  return "MOSTLY CLEAR";
        case 2:  return "PARTLY CLOUDY";
        case 3:  return "OVERCAST";
        case 45:
        case 48: return "FOG";
        case 51:
        case 53:
        case 55: return "DRIZZLE";
        case 56:
        case 57: return "FREEZING DRIZZLE";
        case 61:
        case 63:
        case 65: return "RAIN";
        case 66:
        case 67: return "FREEZING RAIN";
        case 71:
        case 73:
        case 75:
        case 77: return "SNOW";
        case 80:
        case 81:
        case 82: return "SHOWERS";
        case 85:
        case 86: return "SNOW SHOWERS";
        case 95: return "THUNDERSTORM";
        case 96:
        case 99: return "HAIL STORM";
        default: return "UNKNOWN";
        }
    }

    /* ----------------------------------------------------------------
     * Refresh the cached clock fields from the system time
     * ---------------------------------------------------------------- */
    void readClock()
    {
        time_t now = time(nullptr);
        g_clock.synced = (now >= TIME_VALID_EPOCH);

        if (!g_clock.synced)
        {
            return;
        }

        struct tm t;
        localtime_r(&now, &t);

        g_clock.hour24 = t.tm_hour;
        g_clock.minute = t.tm_min;
        g_clock.second = t.tm_sec;
        g_clock.day = t.tm_mday;
        g_clock.month = t.tm_mon + 1;
        g_clock.year = t.tm_year + 1900;
        g_clock.monthName = MONTHS[t.tm_mon];
        g_clock.dayName = DAYS[t.tm_wday];

        g_clock.isPM = (t.tm_hour >= 12);
        int h = t.tm_hour % 12;
        g_clock.hour12 = (h == 0) ? 12 : h;
    }

    /* ----------------------------------------------------------------
     * Approximate location from public IP. Non-fatal on failure.
     * ---------------------------------------------------------------- */
    bool resolveLocationFromIP()
    {
#if LOCATION_FROM_IP
        WiFiClient client;
        HTTPClient http;

        if (!http.begin(client, "http://ip-api.com/json/?fields=status,lat,lon,city"))
        {
            return false;
        }

        if (http.GET() != HTTP_CODE_OK)
        {
            http.end();
            return false;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getString());
        http.end();

        if (err || doc["status"].as<String>() != "success")
        {
            return false;
        }

        g_lat = doc["lat"].as<float>();
        g_lon = doc["lon"].as<float>();

        Serial.printf("[NET] Location: %s (%.4f, %.4f)\n",
                      doc["city"].as<const char *>(), g_lat, g_lon);
        return true;
#else
        return false;
#endif
    }

    /* ----------------------------------------------------------------
     * One weather request. Blocking, but only runs when due.
     * ---------------------------------------------------------------- */
    bool fetchWeather()
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            return false;
        }

        g_fetching = true;

        char url[280];
        snprintf(url, sizeof(url),
                 "https://api.open-meteo.com/v1/forecast"
                 "?latitude=%.4f&longitude=%.4f"
                 "&current_weather=true"
                 "&daily=temperature_2m_max,temperature_2m_min"
                 // timezone=auto so the daily high/low cover the LOCAL day,
                 // which matters once the coordinates come from IP lookup
                 "&forecast_days=1&timezone=auto",
                 g_lat, g_lon);

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient https;
        https.setConnectTimeout(8000);
        https.setTimeout(8000);

        if (!https.begin(client, url))
        {
            Serial.println("[WX] connection failed");
            g_fetching = false;
            return false;
        }

        int status = https.GET();
        if (status != HTTP_CODE_OK)
        {
            Serial.printf("[WX] HTTP %d\n", status);
            https.end();
            g_fetching = false;
            return false;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, https.getString());
        https.end();

        if (err)
        {
            Serial.printf("[WX] JSON error: %s\n", err.c_str());
            g_fetching = false;
            return false;
        }

        JsonObject cur = doc["current_weather"];
        if (cur.isNull())
        {
            Serial.println("[WX] no current_weather");
            g_fetching = false;
            return false;
        }

        g_weather.temp = cur["temperature"].as<float>();
        g_weather.code = cur["weathercode"].as<int>();
        g_weather.isDay = (cur["is_day"].as<int>() != 0);
        g_weather.condition = describeCode(g_weather.code);

        // Daily high/low are optional - keep old values if absent
        JsonArray hi = doc["daily"]["temperature_2m_max"];
        JsonArray lo = doc["daily"]["temperature_2m_min"];
        if (!hi.isNull() && hi.size() > 0)
        {
            g_weather.tempMax = hi[0].as<float>();
        }
        if (!lo.isNull() && lo.size() > 0)
        {
            g_weather.tempMin = lo[0].as<float>();
        }

        g_weather.valid = true;
        g_fetching = false;

        Serial.printf("[WX] %.1fC %s (H %.0f / L %.0f)\n",
                      g_weather.temp, g_weather.condition,
                      g_weather.tempMax, g_weather.tempMin);
        return true;
    }

    void startWifi()
    {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        g_lastWifiAttempt = millis();
        Serial.printf("[NET] connecting to %s\n", WIFI_SSID);
    }
}

/* --------------------------------------------------------------------
 * PUBLIC
 * -------------------------------------------------------------------- */
namespace net
{
    void begin()
    {
        readClock();
        startWifi();
    }

    NetEvents update()
    {
        NetEvents ev;
        const uint32_t now = millis();
        const bool connected = (WiFi.status() == WL_CONNECTED);

        /* ---- WiFi state changes and retries ---- */
        if (connected != g_wasConnected)
        {
            g_wasConnected = connected;
            ev.wifiChanged = true;

            if (connected)
            {
                Serial.printf("[NET] connected, IP %s\n", WiFi.localIP().toString().c_str());

                if (!g_ntpConfigured)
                {
                    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC,
                               "pool.ntp.org", "time.google.com", "time.nist.gov");
                    g_ntpConfigured = true;
                    g_lastNtpAttempt = now;
                }

                if (!g_locationResolved)
                {
                    g_locationResolved = resolveLocationFromIP();
                }

                g_nextWeatherDue = now; // fetch as soon as we are up
            }
            else
            {
                Serial.println("[NET] connection lost");
            }
        }

        if (!connected && (now - g_lastWifiAttempt >= WIFI_RETRY_MS))
        {
            Serial.println("[NET] retrying WiFi");
            WiFi.disconnect();
            startWifi();
        }

        /* ---- clock ---- */
        const bool wasSynced = g_clock.synced;
        readClock();

        if (g_clock.synced && !wasSynced)
        {
            ev.timeJustSynced = true;
            g_lastNtpSuccess = now;
            Serial.println("[NET] time synced");
        }

        if (g_clock.minute != g_lastMinute)
        {
            g_lastMinute = g_clock.minute;
            ev.minuteChanged = true;
        }

        /* ---- NTP: keep trying while unsynced, then drift-correct ---- */
        if (connected && g_ntpConfigured)
        {
            const bool needRetry = !g_clock.synced && (now - g_lastNtpAttempt >= NTP_RETRY_MS);
            const bool needResync = g_clock.synced && (now - g_lastNtpSuccess >= NTP_RESYNC_MS);

            if (needRetry || needResync)
            {
                configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC,
                           "pool.ntp.org", "time.google.com", "time.nist.gov");
                g_lastNtpAttempt = now;
                if (needResync)
                {
                    g_lastNtpSuccess = now;
                }
            }
        }

        /* ---- weather, with backoff so failures never hot-loop ---- */
        if (connected && (int32_t)(now - g_nextWeatherDue) >= 0)
        {
            const bool ok = fetchWeather();
            g_nextWeatherDue = now + (ok ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS);
            ev.weatherChanged = ok;
        }

        return ev;
    }

    bool refreshWeatherNow()
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            return false;
        }

        const bool ok = fetchWeather();
        g_nextWeatherDue = millis() + (ok ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS);
        return ok;
    }

    bool isConnected() { return WiFi.status() == WL_CONNECTED; }
    int rssi() { return WiFi.RSSI(); }
    String ipAddress() { return WiFi.localIP().toString(); }

    const WeatherData &weather() { return g_weather; }
    const ClockData &clock() { return g_clock; }
    bool isFetching() { return g_fetching; }
}
