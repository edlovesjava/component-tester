// =============================================================================
// identify.cpp — auto-detect BJT polarity, pinout, β (hFE), and V_BE.
//
// Background, in one paragraph:
//   A BJT contains TWO PN junctions that share a common base region (B-E and
//   B-C). Each junction, in isolation, behaves roughly like a silicon diode:
//   it drops ~0.6 V when forward-biased and looks effectively open otherwise.
//   The BASE is therefore the unique terminal that connects to BOTH of the
//   other two terminals through a forward-biased diode. Whether that forward
//   bias means "base higher than emitter/collector" (NPN) or "base lower"
//   (PNP) tells us the polarity. Collector vs emitter is decided by trying
//   both orientations and keeping the one with the higher current gain β,
//   because the two terminals are physically asymmetric.
//
// See docs/theory.md and docs/glossary.md for the physics this code is
// implementing, and the test-pin schematic in README.md §"Test Pin Circuit"
// for the hardware those constants below are tied to.
// =============================================================================

#include "identify.h"
#include "../hal/testpins.h"
#include "../hal/adc.h"

extern TestPins tp;
extern ADC adc;

// #define DEBUG_IDENTIFY

// ----- Circuit constants (must match the physical board) ---------------------
// Supply rail. All "drive HIGH" GPIOs end up at this voltage (minus a small
// ESP32 drive drop we ignore).
static constexpr float V_CC = 3.3f;

// Test-pin resistors. See hal/testpins.h for the schematic.
static constexpr float R_LO = 680.0f;      // low-impedance drive (Ω)
static constexpr float R_HI = 470000.0f;   // high-impedance bias (Ω)

// ----- Detection thresholds --------------------------------------------------
// A forward-biased silicon PN junction sits in the 0.55–0.75 V range. We accept
// 0.3–0.9 V to tolerate Schottkys (~0.3 V) and Ge devices on the low end, and
// stacked-junction parts on the high end, without false-triggering on resistor-
// divider voltages near V_CC/2.
static constexpr float V_FWD_MIN = 0.3f;
static constexpr float V_FWD_MAX = 0.9f;

// V_CE saturation floor. Below this, the transistor is in saturation rather
// than the active region, so β = I_C / I_B no longer reflects the device's
// small-signal gain — we reject the measurement instead of reporting a low β.
static constexpr float V_CE_SAT_MIN = 0.2f;

// Minimum β we will accept. Anything below this is more likely a non-BJT (a
// diode pair, a leaky junction, no device at all) than a real transistor.
static constexpr float MIN_HFE = 2.0f;

// Settling times. The 500 µs in the junction probe is enough for the ADS1115
// front end + RC of the test network to settle. The 10 ms in β-measurement
// allows the base current to establish a steady collector current.
static constexpr uint32_t SETTLE_PROBE_US = 500;
static constexpr uint32_t SETTLE_BIAS_MS  = 10;

// Floor on I_B to avoid divide-by-near-zero in the β calculation.
static constexpr float MIN_I_B = 1e-7f;   // 100 nA

// -----------------------------------------------------------------------------

static float readV(uint8_t idx) {
    return adc.readVoltage(idx);
}

// -----------------------------------------------------------------------------
// Junction probing
//
// "Forward probe": drive node `driver` HIGH through 680 Ω, pull the other two
// nodes LOW through 680 Ω. If there is a PN junction whose anode is `driver`
// and cathode is some node `sense`, it will conduct, and we will see
//     V(driver) - V(sense) ≈ 0.6 V
// We record that as forwardJunction[driver][sense] = 1.
//
// "Reverse probe": symmetric in the other direction. Drive `driver` LOW
// through 680 Ω, pull the others HIGH through 470 kΩ (a *weak* pull to avoid
// fighting the strong driver). If there is a PN junction whose anode is the
// sense node and cathode is `driver`, current flows from the weak pull-up
// down through the diode and the 680 Ω, and we see
//     V(sense) - V(driver) ≈ 0.6 V
// We record that as reverseJunction[driver][sense] = 1.
//
// Both matrices use the SAME convention: [driver][sense]. forwardJunction has
// the driven node as anode; reverseJunction has the driven node as cathode.
// -----------------------------------------------------------------------------

static void probeForwardJunctions(uint8_t driver, uint8_t forwardJunction[3][3]) {
    for (uint8_t j = 0; j < 3; j++) {
        tp.setNode(j, (j == driver) ? NodeRole::STRONG_HIGH : NodeRole::STRONG_LOW);
    }
    delayMicroseconds(SETTLE_PROBE_US);
    float vDriver = readV(driver);
#ifdef DEBUG_IDENTIFY
    Serial.printf("P%d FWD drive=%.3f", driver, vDriver);
#endif
    for (uint8_t sense = 0; sense < 3; sense++) {
        if (sense == driver) continue;
        float vSense = readV(sense);
        float drop  = vDriver - vSense;
        bool isDiode = (drop > V_FWD_MIN && drop < V_FWD_MAX);
        forwardJunction[driver][sense] = isDiode ? 1 : 0;
#ifdef DEBUG_IDENTIFY
        Serial.printf(" j%d=%.3f(d=%.3f)", sense, vSense, drop);
#endif
    }
#ifdef DEBUG_IDENTIFY
    Serial.println();
#endif
    tp.allInput();
}

static void probeReverseJunctions(uint8_t driver, uint8_t reverseJunction[3][3]) {
    tp.setNode(driver, NodeRole::STRONG_LOW);
    for (uint8_t sense = 0; sense < 3; sense++) {
        if (sense == driver) continue;
        tp.setNode(sense, NodeRole::WEAK_HIGH);
    }
    delayMicroseconds(SETTLE_PROBE_US);
    float vDriver = readV(driver);
#ifdef DEBUG_IDENTIFY
    Serial.printf("P%d REV drive=%.3f", driver, vDriver);
#endif
    for (uint8_t sense = 0; sense < 3; sense++) {
        if (sense == driver) continue;
        float vSense = readV(sense);
        float drop  = vSense - vDriver;
        bool isDiode = (drop > V_FWD_MIN && drop < V_FWD_MAX);
        reverseJunction[driver][sense] = isDiode ? 1 : 0;
#ifdef DEBUG_IDENTIFY
        Serial.printf(" j%d=%.3f(d=%.3f)", sense, vSense, drop);
#endif
    }
#ifdef DEBUG_IDENTIFY
    Serial.println();
#endif
    tp.allInput();
}

// -----------------------------------------------------------------------------
// β measurement
//
// Two phases that we DELIBERATELY keep separate for clarity:
//   biasForBeta()   — configure the resistor network around the BJT.
//   computeBeta()   — read the three terminal voltages and compute β.
//
// Bias network (NPN; PNP is the rails-swapped mirror):
//
//                V_CC
//                 │
//                 R_HI=470kΩ        (base bias — provides a small, well-
//                 │                  defined I_B; with V_BE ≈ 0.7 V on a 3.3 V
//                 │                  rail this is ~7 µA)
//                 B
//                 │
//        V_CC ── R_LO ── C         (collector pulled toward V_CC through
//                                    680 Ω so I_C produces a measurable drop)
//
//                 E
//                 │
//                R_LO=680Ω          (emitter to GND through 680 Ω: vE/680
//                 │                   IS the emitter current directly)
//                GND
//
// With the device in the active region we expect:
//   I_B  = (V_CC - V_B)  / R_HI     (Ohm's law across base resistor)
//   I_C  = (V_CC - V_C)  / R_LO     (Ohm's law across collector resistor)
//   I_E  =  V_E          / R_LO     (Ohm's law across emitter resistor)
//   β    = I_C / I_B                (definition of DC current gain)
// -----------------------------------------------------------------------------

static void biasForBeta(uint8_t c, uint8_t b, uint8_t e, bool npn) {
    if (npn) {
        tp.setNode(c, NodeRole::STRONG_HIGH);  // collector ← V_CC via R_LO
        tp.setNode(e, NodeRole::STRONG_LOW);   // emitter   → GND  via R_LO
        tp.setNode(b, NodeRole::WEAK_HIGH);    // base bias ← V_CC via R_HI (~7 µA)
    } else {
        // PNP: rails are swapped — collector sinks to GND, emitter sources
        // from V_CC, base is pulled toward GND through R_HI.
        tp.setNode(c, NodeRole::STRONG_LOW);
        tp.setNode(e, NodeRole::STRONG_HIGH);
        tp.setNode(b, NodeRole::WEAK_LOW);
    }
    delay(SETTLE_BIAS_MS);
}

static float computeBeta(uint8_t c, uint8_t b, uint8_t e, bool npn, float* i_e_out) {
    float vB = readV(b);
    float vC = readV(c);
    float vE = readV(e);

    // Saturation guard: in the active region V_CE is at least a few hundred mV.
    // If we are below that, β reported here is meaningless — bail.
    float vCE = npn ? (vC - vE) : (vE - vC);
    if (vCE < V_CE_SAT_MIN) return 0.0f;

    float i_b, i_c, i_e;
    if (npn) {
        // NPN currents (Ohm's law across each bias resistor):
        i_b = (V_CC - vB) / R_HI;   // base resistor drops from V_CC down to vB
        i_c = (V_CC - vC) / R_LO;   // collector resistor drops from V_CC down to vC
        i_e =  vE         / R_LO;   // emitter resistor lifts from GND up to vE
    } else {
        // PNP — mirror image. Current directions reverse; magnitudes are the
        // same formulas with V_CC ↔ GND swapped on each terminal.
        i_b =  vB         / R_HI;
        i_c =  vC         / R_LO;
        i_e = (V_CC - vE) / R_LO;
    }
    if (i_e_out) *i_e_out = i_e;

#ifdef DEBUG_IDENTIFY
    Serial.printf("  HFE c=%d b=%d e=%d vB=%.3f vC=%.3f vE=%.3f i_b=%.2e i_c=%.2e\n",
        c, b, e, vB, vC, vE, i_b, i_c);
#endif

    return (i_b > MIN_I_B) ? (i_c / i_b) : 0.0f;
}

static float measureBeta(uint8_t c, uint8_t b, uint8_t e, bool npn, float* i_e_out) {
    biasForBeta(c, b, e, npn);
    float beta = computeBeta(c, b, e, npn, i_e_out);
    tp.allInput();
    return beta;
}

// -----------------------------------------------------------------------------
// Main identification routine.
// -----------------------------------------------------------------------------

Identification Identifier::identify() {
    Identification result;

    // -------------------------------------------------------------------------
    // Step 1: Probe every (driver, sense) pair to find PN junctions.
    //         Each entry is 1 iff a forward-biased silicon-junction-shaped
    //         voltage drop was observed.
    // -------------------------------------------------------------------------
    uint8_t forwardJunction[3][3] = {};
    uint8_t reverseJunction[3][3] = {};
    for (uint8_t i = 0; i < 3; i++) {
        probeForwardJunctions(i, forwardJunction);
        probeReverseJunctions(i, reverseJunction);
    }

#ifdef DEBUG_IDENTIFY
    for (uint8_t i = 0; i < 3; i++) {
        Serial.printf("  F%d:%d%d%d R%d:%d%d%d\n",
            i, forwardJunction[i][0], forwardJunction[i][1], forwardJunction[i][2],
            i, reverseJunction[i][0], reverseJunction[i][1], reverseJunction[i][2]);
    }
#endif

    // -------------------------------------------------------------------------
    // Step 2: A pin that is the ANODE of two forward-biased junctions is the
    //         base of an NPN. A pin that is the CATHODE of two forward-biased
    //         junctions is the base of a PNP. (See docs/theory.md
    //         #base-detection for the physical argument.)
    // -------------------------------------------------------------------------
    int  basePin = -1;
    bool isNpn   = true;
    for (uint8_t i = 0; i < 3; i++) {
        int forwardFromPin = 0;   // pin i is anode (NPN base signature)
        int reverseFromPin = 0;   // pin i is cathode (PNP base signature)
        for (uint8_t j = 0; j < 3; j++) {
            if (i == j) continue;
            if (forwardJunction[i][j]) forwardFromPin++;
            if (reverseJunction[i][j]) reverseFromPin++;
        }
        if (forwardFromPin == 2) { basePin = i; isNpn = true;  break; }
        if (reverseFromPin == 2) { basePin = i; isNpn = false; break; }
    }

#ifdef DEBUG_IDENTIFY
    Serial.printf("BASE=%d npn=%d\n", basePin, isNpn);
#endif
    if (basePin < 0) {
        result.reason = Reason::NO_BASE_CANDIDATE;
        return result;
    }

    result.type = isNpn ? TransistorType::NPN : TransistorType::PNP;

    // -------------------------------------------------------------------------
    // Step 3: The other two pins are collector and emitter. They are
    //         physically asymmetric (doping profile + geometry), so β is much
    //         larger in the correct orientation. We try BOTH assignments and
    //         keep the one with the higher β.
    // -------------------------------------------------------------------------
    uint8_t other[2];
    uint8_t oi = 0;
    for (uint8_t j = 0; j < 3; j++) {
        if (j != (uint8_t)basePin) other[oi++] = j;
    }

    float ieIfAisCollector = 0, ieIfBisCollector = 0;
    float betaIfAisCollector = measureBeta(other[0], basePin, other[1], isNpn, &ieIfAisCollector);
    float betaIfBisCollector = measureBeta(other[1], basePin, other[0], isNpn, &ieIfBisCollector);

#ifdef DEBUG_IDENTIFY
    Serial.printf("βA=%.1f βB=%.1f npn=%d\n", betaIfAisCollector, betaIfBisCollector, isNpn);
#endif

    if (betaIfAisCollector >= betaIfBisCollector && betaIfAisCollector >= MIN_HFE) {
        result.pins = {other[0], (uint8_t)basePin, other[1]};
        result.hfe  = betaIfAisCollector;
        result.i_e  = ieIfAisCollector;
    } else if (betaIfBisCollector >= MIN_HFE) {
        result.pins = {other[1], (uint8_t)basePin, other[0]};
        result.hfe  = betaIfBisCollector;
        result.i_e  = ieIfBisCollector;
    } else {
        result.type   = TransistorType::UNKNOWN;
        result.reason = Reason::BETA_TOO_LOW;
        return result;
    }

    // -------------------------------------------------------------------------
    // Step 4: With the device biased in the active region, V_BE is just the
    //         base-emitter voltage drop. Bias base and emitter only (no
    //         collector current path), so the reading reflects the forward
    //         diode drop with minimal IR error.
    // -------------------------------------------------------------------------
    if (isNpn) {
        tp.setNode(result.pins.emitter, NodeRole::STRONG_LOW);
        tp.setNode(result.pins.base,    NodeRole::STRONG_HIGH);
    } else {
        tp.setNode(result.pins.emitter, NodeRole::STRONG_HIGH);
        tp.setNode(result.pins.base,    NodeRole::STRONG_LOW);
    }
    delay(SETTLE_BIAS_MS);

    float vB = readV(result.pins.base);
    float vE = readV(result.pins.emitter);
    result.v_be  = isNpn ? (vB - vE) : (vE - vB);
    result.reason = Reason::OK;
    tp.allInput();

    return result;
}

const char* Identifier::typeString(TransistorType t) {
    switch (t) {
        case TransistorType::NPN: return "NPN";
        case TransistorType::PNP: return "PNP";
        default: return "UNKNOWN";
    }
}

const char* Identifier::reasonString(Reason r) {
    switch (r) {
        case Reason::OK:                 return "OK";
        case Reason::NO_BASE_CANDIDATE:  return "No BJT detected";
        case Reason::BETA_TOO_LOW:       return "Beta too low";
    }
    return "?";
}
