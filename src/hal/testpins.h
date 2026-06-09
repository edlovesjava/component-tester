#pragma once
#include <stdint.h>
#include "pins.h"

enum class PinDrive : uint8_t { DRV_LOW, DRV_HIGH, DRV_HIZ };

class TestPins {
public:
    static constexpr uint8_t COUNT = 3;
    void begin();
    void setPin(uint8_t idx, PinDrive lo, PinDrive hi);
    void setPinLo(uint8_t idx, PinDrive d);
    void setPinHi(uint8_t idx, PinDrive d);
    PinDrive getLo(uint8_t idx) const;
    PinDrive getHi(uint8_t idx) const;
    void allInput();

private:
    PinDrive m_lo[COUNT] = {};
    PinDrive m_hi[COUNT] = {};
    static constexpr uint8_t PIN_LO[COUNT] = {PIN_T1_LO, PIN_T2_LO, PIN_T3_LO};
    static constexpr uint8_t PIN_HI[COUNT] = {PIN_T1_HI, PIN_T2_HI, PIN_T3_HI};
};
