# ESP32 Weather & Time Display (ILI9488 + XPT2046)

<p align="center">
  <img src="assets/esp32_ili9488_display.jpg" alt="ESP32 ILI9488 Weather Display" width="480">
</p>

A clean PlatformIO proof-of-concept for building ESP32 display-based applications
with WiFi, NTP time sync, weather API integration, and touch input.

This project is intended as a **reusable reference base** for future ESP32 projects
(BLE, MQTT, Firebase, dashboards, media players, etc).

---

## Hardware

- ESP32 Dev Module
- ILI9488 SPI TFT Display
- XPT2046 Touch Controller

### TFT Pin Mapping

| TFT Pin | ESP32   |
| ------- | ------- |
| MOSI    | GPIO 23 |
| SCLK    | GPIO 18 |
| CS      | GPIO 15 |
| DC      | GPIO 2  |
| RST     | GPIO 4  |

### Touch Pin Mapping

| Touch Pin | ESP32   |
| --------- | ------- |
| CS        | GPIO 22 |
| IRQ       | GPIO 21 |
| MOSI      | GPIO 23 |
| MISO      | GPIO 19 |
| CLK       | GPIO 18 |

---

## Features

- WiFi auto-reconnect (5 min retry)
- NTP time sync (Asia/Kolkata)
- Weather via Open-Meteo API
- IP-based location detection (fallback supported)
- Large time UI (70%) + weather panel (30%)
- WiFi status indicator
- Touch-to-refresh weather

---

## Libraries Used

- TFT_eSPI
- XPT2046_Touchscreen
- ArduinoJson v7

---

## Notes

- Touch calibration handled externally.
- LVGL intentionally avoided to keep code lightweight.

---

## License

MIT
