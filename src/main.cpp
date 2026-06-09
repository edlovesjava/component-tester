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
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(2, 2);

    if (r.valid()) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.printf("%s\n", Identifier::typeString(r.type));
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.printf("C=T%d B=T%d E=T%d\n",
            r.pins.collector + 1, r.pins.base + 1, r.pins.emitter + 1);
        tft.printf("hFE: %.0f\n", r.hfe);
        tft.printf("V_BE: %.3fV\n", r.v_be);
        Serial.printf("%s C=%d B=%d E=%d hFE=%.0f Vbe=%.3f\n",
            Identifier::typeString(r.type),
            r.pins.collector + 1, r.pins.base + 1, r.pins.emitter + 1,
            r.hfe, r.v_be);
    } else {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.print("Insert trans.");
        Serial.println("Waiting...");
    }

    delay(500);
}
