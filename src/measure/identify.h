#pragma once
#include <stdint.h>
#include <Arduino.h>

// BJT (Bipolar Junction Transistor) polarity.
//   NPN: emitter is N-type, base P-type, collector N-type. Arrow on the
//        emitter symbol points outward. Conventional collector current flows
//        from collector to emitter when the base is biased above the emitter.
//   PNP: doping is the mirror image. Arrow points inward. Conventional
//        collector current flows from emitter to collector.
// UNKNOWN is the "nothing usable detected" state.
enum class TransistorType : uint8_t { UNKNOWN, NPN, PNP };

// Which physical test pin (0/1/2 → T1/T2/T3) plays each electrical role.
struct PinAssignment {
    uint8_t collector;
    uint8_t base;
    uint8_t emitter;
    bool valid() const { return collector < 3 && base < 3 && emitter < 3; }
};

// Why an identification attempt failed. Useful as a diagnostic readout —
// see main.cpp, which displays this when valid()==false.
enum class Reason : uint8_t {
    OK,                  // identification succeeded
    NO_BASE_CANDIDATE,   // no pin showed the "two forward junctions" pattern
    BETA_TOO_LOW,        // β below MIN_HFE in both C/E orientations
};

struct Identification {
    TransistorType type = TransistorType::UNKNOWN;
    PinAssignment pins  = {0, 0, 0};
    float v_be = 0.0f;   // base-emitter forward voltage (volts)
    float hfe  = 0.0f;   // β = I_C / I_B, dimensionless
    float i_e  = 0.0f;   // emitter current at the test bias (amps)
    Reason reason = Reason::NO_BASE_CANDIDATE;

    bool valid() const {
        return type != TransistorType::UNKNOWN && pins.valid();
    }
};

class Identifier {
public:
    Identification identify();
    static const char* typeString(TransistorType t);
    static const char* reasonString(Reason r);
};
