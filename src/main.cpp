#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <ArduinoJson.h>
#include <time.h>

/* ================= TOUCH PINS ================= */
#define T_CS 22
#define T_IRQ 21
#define T_CLK 18
#define T_MISO 19
#define T_MOSI 23

/* ================= WIFI CONFIG ================= */
const char *WIFI_SSID = "YourWiFiSSID";
const char *WIFI_PASS = "YourWiFiPassword";

/* ================= TIMERS ================= */
constexpr uint32_t WEATHER_REFRESH_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t WIFI_RETRY_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t UI_REFRESH_MS = 30UL * 1000UL;

/* ================= UI COLORS ================= */
#define COL_BG TFT_BLACK
#define COL_TIME TFT_WHITE
#define COL_ACCENT TFT_CYAN
#define COL_WEATHER TFT_YELLOW
#define COL_WIFI_OK TFT_GREEN
#define COL_WIFI_BAD TFT_RED

/* ================= OBJECTS ================= */
TFT_eSPI tft;
XPT2046_Touchscreen ts(T_CS, T_IRQ);

/* ================= STATE ================= */
float latitude = 12.9716f; // default fallback
float longitude = 77.5946f;

float weatherTemp = NAN;
int weatherCode = -1;
String weatherText = "---";

uint32_t lastWeatherMs = 0;
uint32_t lastWifiAttempt = 0;
uint32_t lastUiDraw = 0;

/* ================= WEATHER CODE MAP ================= */
void mapWeatherCode(int code, String &out)
{
    switch (code)
    {
    case 0:
        out = "Clear";
        break;
    case 1:
        out = "Mostly Clear";
        break;
    case 2:
        out = "Partly Cloudy";
        break;
    case 3:
        out = "Overcast";
        break;
    case 45:
    case 48:
        out = "Fog";
        break;
    case 61:
    case 63:
    case 65:
        out = "Rain";
        break;
    case 71:
    case 73:
    case 75:
        out = "Snow";
        break;
    case 80:
    case 81:
    case 82:
        out = "Showers";
        break;
    case 95:
        out = "Thunder";
        break;
    default:
        out = "Unknown";
        break;
    }
}

/* ================= WIFI ================= */
void connectWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("WiFi connect initiated");
}

void wifiMaintain()
{
    if (WiFi.status() == WL_CONNECTED)
        return;
    if (millis() - lastWifiAttempt < WIFI_RETRY_MS)
        return;

    lastWifiAttempt = millis();
    Serial.println("WiFi retry...");
    WiFi.disconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

/* ================= TIME ================= */
void syncTime()
{
    configTime(19800, 0, "pool.ntp.org", "time.google.com");

    time_t now;
    for (uint8_t i = 0; i < 10; i++)
    {
        now = time(nullptr);
        if (now > 100000)
            return;
        delay(500);
    }
    Serial.println("NTP sync failed");
}

/* ================= LOCATION (IP) ================= */
bool fetchLocationFromIP()
{
    WiFiClient client;
    HTTPClient http;

    if (!http.begin(client, "http://ip-api.com/json"))
        return false;
    if (http.GET() != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err || doc["status"] != "success")
        return false;

    latitude = doc["lat"].as<float>();
    longitude = doc["lon"].as<float>();

    Serial.printf("Location: %.4f, %.4f\n", latitude, longitude);
    return true;
}

/* ================= WEATHER ================= */
bool fetchWeather()
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?"
             "latitude=%f&longitude=%f&current_weather=true&timezone=Asia%%2FKolkata",
             latitude, longitude);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    if (!https.begin(client, url))
        return false;
    if (https.GET() != HTTP_CODE_OK)
    {
        https.end();
        return false;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, https.getString());
    https.end();
    if (err)
        return false;

    JsonVariant cw = doc["current_weather"];
    if (cw.isNull())
        return false;

    weatherTemp = cw["temperature"].as<float>();
    weatherCode = cw["weathercode"].as<int>();
    mapWeatherCode(weatherCode, weatherText);

    return true;
}

/* ================= UI ================= */
void drawUI()
{
    tft.fillScreen(COL_BG);

    int w = tft.width();
    int h = tft.height();
    int timeW = w * 0.7;

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    char timeStr[6];
    char dateStr[24];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &t);
    strftime(dateStr, sizeof(dateStr), "%a %d %b %Y", &t);

    tft.setTextDatum(MC_DATUM);

    // TIME
    tft.setTextFont(6);
    tft.setTextColor(COL_TIME, COL_BG);
    tft.drawString(timeStr, timeW / 2, h / 2 - 20);

    tft.setTextFont(2);
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.drawString(dateStr, timeW / 2, h / 2 + 35);

    // WEATHER
    tft.drawFastVLine(timeW, 10, h - 20, TFT_DARKGREY);
    int wx = timeW + (w - timeW) / 2;

    tft.setTextFont(4);
    tft.setTextColor(COL_WEATHER, COL_BG);
    String temp = isnan(weatherTemp) ? "--°C" : String(weatherTemp, 1) + "°C";
    tft.drawString(temp, wx, 110);

    tft.setTextFont(2);
    tft.drawString(weatherText, wx, 160);

    // WIFI STATUS
    uint16_t wifiColor = (WiFi.status() == WL_CONNECTED) ? COL_WIFI_OK : COL_WIFI_BAD;
    tft.fillCircle(w - 15, 15, 6, wifiColor);
}

/* ================= SETUP ================= */
void setup()
{
    Serial.begin(115200);

    tft.init();
    tft.setRotation(3); // DO NOT CHANGE
    tft.fillScreen(COL_BG);

    SPI.begin(T_CLK, T_MISO, T_MOSI);
    pinMode(T_IRQ, INPUT_PULLUP);
    ts.begin();

    connectWiFi();
    syncTime();

    if (WiFi.status() == WL_CONNECTED)
    {
        fetchLocationFromIP();
        fetchWeather();
    }

    lastWeatherMs = millis();
    drawUI();
}

/* ================= LOOP ================= */
void loop()
{
    wifiMaintain();

    if (millis() - lastUiDraw > UI_REFRESH_MS)
    {
        lastUiDraw = millis();
        drawUI();
    }

    if (WiFi.status() == WL_CONNECTED &&
        millis() - lastWeatherMs > WEATHER_REFRESH_MS)
    {
        fetchWeather();
        lastWeatherMs = millis();
    }

    if (ts.touched())
    {
        delay(200);
        if (WiFi.status() == WL_CONNECTED)
            fetchWeather();
        drawUI();
    }
}
