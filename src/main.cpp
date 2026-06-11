#include <Arduino.h>
#include <Wire.h>
#include "hal/pins.h"
#include "hal/adc.h"
#include "hal/testpins.h"
#include "hal/stimulus.h"
#include "measure/identify.h"
#include <TFT_eSPI.h>

ADC adc;
TestPins tp;
Stimulus stim;
Identifier id;
TFT_eSPI tft;

static void drawType(const char* type) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawCentreString(type, 80, 2, 1);
}

static void drawSymbol(bool npn, int c, int b, int e) {
    tft.fillRect(77, 30, 3, 16, TFT_WHITE);
    tft.drawCircle(78, 38, 12, TFT_WHITE);
    tft.drawLine(92, 24, 78, 38, TFT_WHITE);
    tft.drawFastHLine(58, 38, 20, TFT_WHITE);
    tft.drawLine(78, 38, 92, 52, TFT_WHITE);

    if (npn) {
        tft.fillTriangle(86, 46, 80, 44, 84, 40, TFT_WHITE);
    } else {
        tft.fillTriangle(80, 40, 82, 46, 86, 42, TFT_WHITE);
    }

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(94, 22); tft.print("C=T"); tft.print(c);
    tft.setCursor(36, 36); tft.print("B=T"); tft.print(b);
    tft.setCursor(94, 52); tft.print("E=T"); tft.print(e);
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

static void drawWaiting() {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawCentreString("Insert transistor", 80, 55, 1);
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
        drawWaiting();
        Serial.println("Waiting...");
    }

    delay(500);
}
