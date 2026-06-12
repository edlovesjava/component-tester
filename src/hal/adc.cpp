#include "adc.h"
#include "pins.h"
#include <Wire.h>

bool ADC::begin(int address) {
    m_ok = m_ads.begin(address);
    if (m_ok) {
        m_ads.setGain(GAIN_ONE);        // ±4.096 V
        m_ads.setDataRate(RATE_ADS1115_860SPS);
    }
    return m_ok;
}

float ADC::readVoltage(uint8_t channel) {
    return voltageForRaw(readRaw(channel));
}

int16_t ADC::readRaw(uint8_t channel) {
    if (!m_ok) return 0;
    return m_ads.readADC_SingleEnded(channel);
}

float ADC::voltageForRaw(int16_t raw) const {
    // ADS1115 in GAIN_ONE: ±4.096 V full-scale, 16-bit signed code.
    //   one LSB ≈ 4.096 V / 32768 ≈ 125 µV — fine for millivolt-level V_BE.
    // We rely on the chip's internal reference; no host-side calibration.
    return raw * 4.096f / 32768.0f;
}
