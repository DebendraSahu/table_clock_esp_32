/*
 * --------------------------------------------------------------------
 * ui.h - all rendering for the split-dashboard clock
 * --------------------------------------------------------------------
 * The two regions that change often (time, temperature) are drawn into
 * sprites and pushed in one blit, so nothing flickers. Everything else
 * is static chrome repainted only when its data changes.
 * --------------------------------------------------------------------
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "net.h"

namespace ui
{
    void begin();

    // The shared display instance - main.cpp needs it for touch
    TFT_eSPI &display();

    // Theme
    void setNight(bool on);
    bool isNight();

    // Full repaint - background, divider, and every region
    void drawAll();

    // Individual regions
    void drawTime();
    void drawDate();
    void drawWeather();
    void drawWifi();

    // Boot / status screens
    void showBoot(const char *line1, const char *line2);

    // Transient message along the bottom
    void showToast(const char *msg, uint16_t color);
    void clearToastIfExpired();

    // Palette access for callers that need a status colour
    uint16_t colorOk();
    uint16_t colorWarn();
    uint16_t colorError();
}
