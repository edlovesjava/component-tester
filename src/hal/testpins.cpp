#include "testpins.h"
#include <Arduino.h>

// C++14 ODR-definitions for the constexpr static arrays declared in the header.
// Harmless on C++17 (where the in-class declaration is a definition); kept here
// so the toolchain default doesn't matter.
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

void TestPins::setPin(uint8_t idx, PinDrive loZ, PinDrive hiZ) {
    if (idx >= COUNT) return;
    setPinLo(idx, loZ);
    setPinHi(idx, hiZ);
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

void TestPins::setNode(uint8_t idx, NodeRole role) {
    switch (role) {
        case NodeRole::OPEN:
            setPin(idx, PinDrive::DRV_HIZ, PinDrive::DRV_HIZ); break;
        case NodeRole::STRONG_HIGH:
            // 680 Ω → V_CC; 470 kΩ path released (so it can't fight the strong drive)
            setPin(idx, PinDrive::DRV_HIGH, PinDrive::DRV_HIZ); break;
        case NodeRole::STRONG_LOW:
            setPin(idx, PinDrive::DRV_LOW,  PinDrive::DRV_HIZ); break;
        case NodeRole::WEAK_HIGH:
            // 470 kΩ → V_CC; 680 Ω path released so the high impedance dominates
            setPin(idx, PinDrive::DRV_HIZ,  PinDrive::DRV_HIGH); break;
        case NodeRole::WEAK_LOW:
            setPin(idx, PinDrive::DRV_HIZ,  PinDrive::DRV_LOW); break;
    }
}

PinDrive TestPins::getLo(uint8_t idx) const {
    return (idx < COUNT) ? m_lo[idx] : PinDrive::DRV_HIZ;
}

PinDrive TestPins::getHi(uint8_t idx) const {
    return (idx < COUNT) ? m_hi[idx] : PinDrive::DRV_HIZ;
}

void TestPins::allInput() {
    for (uint8_t i = 0; i < COUNT; i++) {
        setNode(i, NodeRole::OPEN);
    }
}
