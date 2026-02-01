/*
 * --------------------------------------------------------------------
 * TOUCH DIAGNOSTIC - standalone, not part of the clock firmware
 * --------------------------------------------------------------------
 * Build and run with:
 *
 *     pio run -e touch-test -t upload -t monitor
 *
 * Proves the XPT2046 is wired and talking, in four stages, so a failure
 * tells you WHICH part is broken rather than just "touch doesn't work":
 *
 *   1. Display alive        - corner blocks and a centre cross
 *   2. Controller responds  - raw Z pressure reported on serial
 *   3. Panel works          - raw X/Y sweep as you drag a finger
 *   4. Calibrated coords    - a dot lands under your finger
 *
 * Stage 2 is the decisive one and self-diagnoses:
 *   Z == 0     -> MISO or T_CS not connected (or TFT_MISO still -1)
 *   Z > 3000   -> MISO railed; the display's SDO is probably wired,
 *                 and it must be left unconnected
 *
 * Everything drawn here is blocks, lines and circles - no text - so the
 * diagnostic still works if the font configuration is broken.
 *
 * Pins and bus speeds come from platformio.ini build_flags, shared with
 * the main firmware via the [env] section.
 * --------------------------------------------------------------------
 */

#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 320

// Set to true after you have run calibration once and pasted the values below
#define USE_STORED_CALIBRATION false
uint16_t calData[5] = {300, 3600, 300, 3600, 3};

/* --------------------------------------------------------------------
 * STAGE 1 - display sanity check, drawn without any font
 * -------------------------------------------------------------------- */
void drawCornerMarkers()
{
    tft.fillScreen(TFT_BLACK);

    // Four corner blocks prove rotation and extents are correct
    tft.fillRect(0, 0, 40, 40, TFT_RED);                          // top-left
    tft.fillRect(SCREEN_WIDTH - 40, 0, 40, 40, TFT_GREEN);        // top-right
    tft.fillRect(0, SCREEN_HEIGHT - 40, 40, 40, TFT_BLUE);        // bottom-left
    tft.fillRect(SCREEN_WIDTH - 40, SCREEN_HEIGHT - 40,
                 40, 40, TFT_YELLOW);                             // bottom-right

    // Centre cross
    tft.drawFastHLine(SCREEN_WIDTH / 2 - 20, SCREEN_HEIGHT / 2, 40, TFT_WHITE);
    tft.drawFastVLine(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 20, 40, TFT_WHITE);
}

/* --------------------------------------------------------------------
 * STAGE 2 - is the controller answering at all?
 * -------------------------------------------------------------------- */
void reportIdlePressure()
{
    Serial.println("[TOUCH] Sampling idle pressure - DO NOT touch the screen...");
    delay(1500);

    uint32_t sum = 0;
    for (int i = 0; i < 20; i++)
    {
        sum += tft.getTouchRawZ();
        delay(20);
    }
    uint16_t idle = sum / 20;

    Serial.printf("[TOUCH] Idle Z = %u\n", idle);

    if (idle == 0)
    {
        Serial.println("[TOUCH] FAIL: reading 0. T_CS or MISO is not connected,");
        Serial.println("        or TFT_MISO is still -1 in User_Setup.h.");
    }
    else if (idle > 3000)
    {
        Serial.println("[TOUCH] FAIL: reading railed high. MISO likely shorted -");
        Serial.println("        check that display SDO is left UNCONNECTED.");
    }
    else
    {
        Serial.println("[TOUCH] OK: controller is responding.");
    }
}

/* --------------------------------------------------------------------
 * STAGE 3 + 4 - calibration
 * -------------------------------------------------------------------- */
void runCalibration()
{
    Serial.println("\n[TOUCH] Calibration: touch each arrow as it appears.");
    Serial.println("        (No on-screen text - fonts are disabled.)\n");

    tft.fillScreen(TFT_BLACK);
    tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 20);

    Serial.println("[TOUCH] Calibration complete. Paste this into the sketch:");
    Serial.print("        uint16_t calData[5] = {");
    for (uint8_t i = 0; i < 5; i++)
    {
        Serial.print(calData[i]);
        if (i < 4) Serial.print(", ");
    }
    Serial.println("};\n");
}

/* --------------------------------------------------------------------
 * SETUP
 * -------------------------------------------------------------------- */
void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n========================================");
    Serial.println("  XPT2046 TOUCH DIAGNOSTIC");
    Serial.println("========================================\n");

    tft.init();
    tft.setRotation(3);

    drawCornerMarkers();
    Serial.println("[DISPLAY] Corner blocks drawn: red TL, green TR, blue BL, yellow BR.");
    Serial.println("[DISPLAY] If you see those, the display half of the bus is fine.\n");
    delay(2000);

    reportIdlePressure();

    if (USE_STORED_CALIBRATION)
    {
        tft.setTouch(calData);
        Serial.println("[TOUCH] Using stored calibration.");
    }
    else
    {
        runCalibration();
    }

    drawCornerMarkers();
    Serial.println("[TOUCH] Ready. Drag a finger - dots follow, values stream below.\n");
}

/* --------------------------------------------------------------------
 * LOOP
 * -------------------------------------------------------------------- */
void loop()
{
    uint16_t rawX = 0, rawY = 0;
    uint16_t z = tft.getTouchRawZ();

    if (tft.getTouchRaw(&rawX, &rawY))
    {
        uint16_t x = 0, y = 0;

        if (tft.getTouch(&x, &y))
        {
            // Dot under the finger - the visual proof touch is mapped correctly
            tft.fillCircle(x, y, 4, TFT_CYAN);
            Serial.printf("[TOUCH] raw(%4u,%4u) z=%4u  ->  screen(%3u,%3u)\n",
                          rawX, rawY, z, x, y);
        }
        else
        {
            // Panel responded but the point fell outside the calibrated area
            Serial.printf("[TOUCH] raw(%4u,%4u) z=%4u  ->  off-screen\n",
                          rawX, rawY, z);
        }
    }

    delay(30);
}
