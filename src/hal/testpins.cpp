#include "testpins.h"
#include <Arduino.h>

const uint8_t TestPins::PIN_LO[TestPins::COUNT];
const uint8_t TestPins::PIN_HI[TestPins::COUNT];

static void setGpio(uint8_t pin, PinDrive d) {
    switch (d) {
        case PinDrive::DRV_LOW:  pinMode(pin, OUTPUT); digitalWrite(pin, 0); break;
        case PinDrive::DRV_HIGH: pinMode(pin, OUTPUT); digitalWrite(pin, 1); break;
        case PinDrive::DRV_HIZ:  pinMode(pin, INPUT); break;
    }
}

void TestPins::begin() {
    allInput();
}

void TestPins::setPin(uint8_t idx, PinDrive lo, PinDrive hi) {
    if (idx >= COUNT) return;
    setPinLo(idx, lo);
    setPinHi(idx, hi);
}

void TestPins::setPinLo(uint8_t idx, PinDrive d) {
    if (idx >= COUNT) return;
    setGpio(PIN_LO[idx], d);
    m_lo[idx] = d;
}

void TestPins::setPinHi(uint8_t idx, PinDrive d) {
    if (idx >= COUNT) return;
    setGpio(PIN_HI[idx], d);
    m_hi[idx] = d;
}

PinDrive TestPins::getLo(uint8_t idx) const {
    return (idx < COUNT) ? m_lo[idx] : PinDrive::DRV_HIZ;
}

PinDrive TestPins::getHi(uint8_t idx) const {
    return (idx < COUNT) ? m_hi[idx] : PinDrive::DRV_HIZ;
}

void TestPins::allInput() {
    for (uint8_t i = 0; i < COUNT; i++) {
        setPin(i, PinDrive::DRV_HIZ, PinDrive::DRV_HIZ);
    }
}
