#pragma once
#include <stdint.h>
#include "pins.h"

// Each test node (T1/T2/T3) is connected to the device under test through
// TWO Thévenin-equivalent sources:
//
//     V_CC ──┐                                ┌── V_CC
//            R_LO=680Ω (low-impedance path)   R_HI=470kΩ (high-impedance path)
//     GND ───┤                                ├── GND
//            │              T(n)              │
//            ●────────────────●────────────────●────► to ADS1115 input
//
// The "LO" GPIO drives through 680 Ω — a stiff source/sink, used as the main
// collector/emitter rail when biasing the BJT.
// The "HI" GPIO drives through 470 kΩ — a current-limited bias, used to inject
// a small base current (~7 µA at 3.3 V) without saturating the device.
// Either GPIO can be driven HIGH (to V_CC), driven LOW (to GND), or set to
// Hi-Z (input mode, effectively disconnected).
enum class PinDrive : uint8_t { DRV_LOW, DRV_HIGH, DRV_HIZ };

// NodeRole expresses what the *node* should look like to the DUT, without the
// caller having to think about which GPIO is which. Each role corresponds to
// one (loZ, hiZ) combination on the underlying TestPins::setPin call.
enum class NodeRole : uint8_t {
    OPEN,            // both GPIOs Hi-Z → node floats (Hi-Z to DUT)
    STRONG_HIGH,     // V_CC via 680 Ω  → low-Z source (e.g. NPN collector rail)
    STRONG_LOW,      // GND via 680 Ω   → low-Z sink   (e.g. NPN emitter rail)
    WEAK_HIGH,       // V_CC via 470 kΩ → current-limited bias (NPN base bias)
    WEAK_LOW,        // GND via 470 kΩ  → current-limited bias (PNP base bias)
};

class TestPins {
public:
    static constexpr uint8_t COUNT = 3;

    void begin();

    // Set both Thévenin sources on node `idx`. `loZ` controls the 680 Ω path,
    // `hiZ` controls the 470 kΩ path. The two names refer to the *resistor*
    // impedance, not the direction of drive — DRV_HIGH/DRV_LOW select polarity.
    void setPin(uint8_t idx, PinDrive loZ, PinDrive hiZ);
    void setPinLo(uint8_t idx, PinDrive d);   // 680 Ω path only
    void setPinHi(uint8_t idx, PinDrive d);   // 470 kΩ path only

    // Higher-level API: configure the node by what role it plays in the
    // measurement, not by GPIO mechanics. Strongly preferred at call sites.
    void setNode(uint8_t idx, NodeRole role);

    PinDrive getLo(uint8_t idx) const;
    PinDrive getHi(uint8_t idx) const;
    void allInput();   // all nodes OPEN (Hi-Z)

private:
    PinDrive m_lo[COUNT] = {};
    PinDrive m_hi[COUNT] = {};
    static constexpr uint8_t PIN_LO[COUNT] = {PIN_T1_LO, PIN_T2_LO, PIN_T3_LO};
    static constexpr uint8_t PIN_HI[COUNT] = {PIN_T1_HI, PIN_T2_HI, PIN_T3_HI};
};
