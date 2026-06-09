#pragma once
#include <Adafruit_ADS1X15.h>
#include "pins.h"

class ADC {
public:
    bool begin(int address = ADS1115_ADDR);
    float readVoltage(uint8_t channel);
    int16_t readRaw(uint8_t channel);
    float voltageForRaw(int16_t raw) const;
private:
    Adafruit_ADS1115 m_ads;
    bool m_ok = false;
};
