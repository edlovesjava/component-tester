#pragma once
#include <stdint.h>
#include <Arduino.h>

enum class TransistorType : uint8_t { UNKNOWN, NPN, PNP };

struct PinAssignment {
    uint8_t collector;
    uint8_t base;
    uint8_t emitter;
    bool valid() const { return collector < 3 && base < 3 && emitter < 3; }
};

struct Identification {
    TransistorType type = TransistorType::UNKNOWN;
    PinAssignment pins = {0, 0, 0};
    float v_be = 0.0f;
    float hfe = 0.0f;
    float i_e = 0.0f;
    bool valid() const {
        return type != TransistorType::UNKNOWN && pins.valid();
    }
};

class Identifier {
public:
    Identification identify();
    static const char* typeString(TransistorType t);
};
