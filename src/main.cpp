// =============================================================================
// main.cpp — top-level loop: identify the inserted BJT, draw the result.
//
// Loop body, in plain English:
//   1. Ask Identifier::identify() what is in the socket.
//   2. Clear the screen.
//   3. If we have a real identification, draw the schematic symbol with the
//      detected C/B/E pin numbers and the measured β, I_E, V_BE.
//      Otherwise draw a status line (insert a transistor, or why detection
//      failed).
//   4. Echo the same information on the serial port (for log-and-script use).
//
// See docs/theory.md for what β / V_BE / V_CE actually mean and
// docs/glossary.md for every abbreviation that appears below.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include "hal/pins.h"
#include "hal/adc.h"
#include "hal/testpins.h"
#include "hal/stimulus.h"
#include "measure/identify.h"
#include <TFT_eSPI.h>

ADC        adc;
TestPins   tp;
Stimulus   stim;
Identifier id;
TFT_eSPI   tft;

// -----------------------------------------------------------------------------
// Schematic symbol layout. Anatomy of the BJT symbol we are drawing on a
// 160×128 ST7735 (rotated landscape):
//
//          C(c) |
//              /            ← upper diagonal (collector lead)
//             /             ← arrowhead lives here for PNP
//   B(b) ──── |              ← vertical bar = base region of the silicon
//             \             ← arrowhead lives here for NPN
//              \           ← lower diagonal (emitter lead)
//          E(e) |
//
// The arrow on the emitter lead is the only thing that visually distinguishes
// NPN from PNP. Standard mnemonic: "Not Pointing iN" → arrow points OUT of the
// transistor body for NPN, INTO it for PNP.
// -----------------------------------------------------------------------------
struct SymbolGeom {
    int barX, barY, barW, barH;   // vertical base bar
    int circleX, circleY, circleR;
    int collectorTipX, collectorTipY;
    int baseLeadX, baseLeadY, baseLeadLen;
    int emitterTipX, emitterTipY;
    int junctionX, junctionY;
};
static constexpr SymbolGeom SYM = {
    77, 30, 3, 16,    // base bar
    78, 38, 12,       // body circle
    92, 24,           // collector tip
    58, 38, 20,       // base lead start + length
    92, 52,           // emitter tip
    78, 38,           // base/lead junction point
};

static void drawType(const char* type) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawCentreString(type, 80, 2, 1);
}

static void drawSymbol(bool npn, int c, int b, int e) {
    // Base bar (the vertical line representing the base region of the silicon).
    tft.fillRect(SYM.barX, SYM.barY, SYM.barW, SYM.barH, TFT_WHITE);
    // Transistor body circle.
    tft.drawCircle(SYM.circleX, SYM.circleY, SYM.circleR, TFT_WHITE);
    // Collector lead (upper diagonal).
    tft.drawLine(SYM.collectorTipX, SYM.collectorTipY,
                 SYM.junctionX,     SYM.junctionY, TFT_WHITE);
    // Base lead (horizontal in to the bar).
    tft.drawFastHLine(SYM.baseLeadX, SYM.baseLeadY, SYM.baseLeadLen, TFT_WHITE);
    // Emitter lead (lower diagonal).
    tft.drawLine(SYM.junctionX,    SYM.junctionY,
                 SYM.emitterTipX,  SYM.emitterTipY, TFT_WHITE);

    // Arrow on the emitter lead — "Not Pointing iN":
    //   NPN: arrow points away from the base bar (outward → emitter tip).
    //   PNP: arrow points toward the base bar (inward → emitter region).
    if (npn) {
        tft.fillTriangle(86, 46, 80, 44, 84, 40, TFT_WHITE);
    } else {
        tft.fillTriangle(80, 40, 82, 46, 86, 42, TFT_WHITE);
    }

    // Pin labels: C(n)/B(n)/E(n) where n is the physical socket pin (1..3).
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(94, 22); tft.print("C("); tft.print(c); tft.print(")");
    tft.setCursor(36, 36); tft.print("B("); tft.print(b); tft.print(")");
    tft.setCursor(94, 52); tft.print("E("); tft.print(e); tft.print(")");
}

static void drawSeparator() {
    tft.drawFastHLine(8, 74, 144, TFT_DARKGREY);
}

static void drawMeasurements(float hfe, float ie, float vbe) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);

    char buf[24];
    tft.setCursor(12, 80);
    snprintf(buf, sizeof(buf), "hFE: %.0f", hfe);
    tft.print(buf);

    tft.setCursor(12, 92);
    snprintf(buf, sizeof(buf), "Ie:  %.2f mA", ie * 1000.0f);
    tft.print(buf);

    tft.setCursor(12, 104);
    snprintf(buf, sizeof(buf), "Vbe: %.0f mV", vbe * 1000.0f);
    tft.print(buf);
}

static void drawWaiting(Reason r) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawCentreString("Insert transistor", 80, 50, 1);
    // Echo the diagnostic reason from identify(): useful when a device is in
    // the socket but rejected (e.g. β too low, or no base candidate found).
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString(Identifier::reasonString(r), 80, 64, 1);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    tft.init();
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(2, 2);
    tft.print("Transistor Tester");
    tft.setCursor(2, 12);
    tft.print("Init...");

    bool ok = true;
    if (!adc.begin()) { Serial.println("ADC FAIL"); ok = false; }
    tp.begin();
    stim.begin();

    if (ok) {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(2, 2);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.print("Ready");
        Serial.println("Ready");
    }
}

void loop() {
    Identification r = id.identify();
    tft.fillScreen(TFT_BLACK);

    if (r.valid()) {
        drawType(Identifier::typeString(r.type));
        drawSymbol(r.type == TransistorType::NPN,
            r.pins.collector + 1, r.pins.base + 1, r.pins.emitter + 1);
        drawSeparator();
        drawMeasurements(r.hfe, r.i_e, r.v_be);
        Serial.printf("%s C=%d B=%d E=%d hFE=%.0f Ie=%.3f Vbe=%.3f\n",
            Identifier::typeString(r.type),
            r.pins.collector + 1, r.pins.base + 1, r.pins.emitter + 1,
            r.hfe, r.i_e, r.v_be);
    } else {
        drawWaiting(r.reason);
        Serial.printf("Waiting... (%s)\n", Identifier::reasonString(r.reason));
    }

    delay(500);
}
