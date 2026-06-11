#include "identify.h"
#include "../hal/testpins.h"
#include "../hal/adc.h"

extern TestPins tp;
extern ADC adc;

// #define DEBUG_IDENTIFY

static constexpr float V_FWD_MIN = 0.3f;
static constexpr float V_FWD_MAX = 0.9f;
static constexpr float MIN_HFE = 2.0f;

static float readV(uint8_t idx) {
    return adc.readVoltage(idx);
}

// Forward probe: drive HIGH, pull others LOW through 680Ω
static void probeFwd(uint8_t drive, uint8_t fwd[3][3]) {
    for (uint8_t j = 0; j < 3; j++) {
        tp.setPin(j, (j == drive) ? PinDrive::DRV_HIGH : PinDrive::DRV_LOW, PinDrive::DRV_HIZ);
    }
    delayMicroseconds(500);
    float vDrive = readV(drive);
#ifdef DEBUG_IDENTIFY
    Serial.printf("P%d FWD drive=%.3f", drive, vDrive);
#endif
    for (uint8_t j = 0; j < 3; j++) {
        if (j == drive) continue;
        float vj = readV(j);
        float v = vDrive - vj;
        fwd[drive][j] = (v > V_FWD_MIN && v < V_FWD_MAX) ? 1 : 0;
#ifdef DEBUG_IDENTIFY
        Serial.printf(" j%d=%.3f(d=%.3f)", j, vj, v);
#endif
    }
#ifdef DEBUG_IDENTIFY
    Serial.println();
#endif
    tp.allInput();
}

// Reverse probe: drive LOW, weak pull-up others through 470kΩ
static void probeRev(uint8_t drive, uint8_t rev[3][3]) {
    tp.setPin(drive, PinDrive::DRV_LOW, PinDrive::DRV_HIZ);
    for (uint8_t j = 0; j < 3; j++) {
        if (j == drive) continue;
        tp.setPinLo(j, PinDrive::DRV_HIZ);
        tp.setPinHi(j, PinDrive::DRV_HIGH);
    }
    delayMicroseconds(500);
    float vDrive = readV(drive);
#ifdef DEBUG_IDENTIFY
    Serial.printf("P%d REV drive=%.3f", drive, vDrive);
#endif
    for (uint8_t j = 0; j < 3; j++) {
        if (j == drive) continue;
        float vj = readV(j);
        float v = vj - vDrive;
        rev[j][drive] = (v > V_FWD_MIN && v < V_FWD_MAX) ? 1 : 0;
#ifdef DEBUG_IDENTIFY
        Serial.printf(" j%d=%.3f(d=%.3f)", j, vj, v);
#endif
    }
#ifdef DEBUG_IDENTIFY
    Serial.println();
#endif
    tp.allInput();
}

static float measureHfe(uint8_t c, uint8_t b, uint8_t e, bool npn, float* i_e = nullptr) {
    if (npn) {
        tp.setPin(c, PinDrive::DRV_HIGH, PinDrive::DRV_HIZ);
        tp.setPin(e, PinDrive::DRV_LOW, PinDrive::DRV_HIZ);
        tp.setPinLo(b, PinDrive::DRV_HIZ);
        tp.setPinHi(b, PinDrive::DRV_HIGH);
    } else {
        tp.setPin(c, PinDrive::DRV_LOW, PinDrive::DRV_HIZ);
        tp.setPin(e, PinDrive::DRV_HIGH, PinDrive::DRV_HIZ);
        tp.setPinLo(b, PinDrive::DRV_HIZ);
        tp.setPinHi(b, PinDrive::DRV_LOW);
    }
    delay(10);

    float vB = readV(b);
    float vC = readV(c);
    float vE = readV(e);
    tp.allInput();

    if (npn && (vC - vE) < 0.2f) return 0;
    if (!npn && (vE - vC) < 0.2f) return 0;

    float i_b, i_c;
    if (npn) {
        i_b = (3.3f - vB) / 470000.0f;
        i_c = (3.3f - vC) / 680.0f;
        if (i_e) *i_e = vE / 680.0f;
    } else {
        i_b = vB / 470000.0f;
        i_c = vC / 680.0f;
        if (i_e) *i_e = (3.3f - vE) / 680.0f;
    }

#ifdef DEBUG_IDENTIFY
    Serial.printf("  HFE c=%d b=%d e=%d vB=%.3f vC=%.3f vE=%.3f i_b=%.2e i_c=%.2e\n",
        c, b, e, vB, vC, vE, i_b, i_c);
#endif

    return (i_b > 1e-7f) ? (i_c / i_b) : 0;
}

Identification Identifier::identify() {
    Identification r;

    uint8_t fwd[3][3] = {}, rev[3][3] = {};
    for (uint8_t i = 0; i < 3; i++) {
        probeFwd(i, fwd);
        probeRev(i, rev);
    }

#ifdef DEBUG_IDENTIFY
    for (uint8_t i = 0; i < 3; i++) {
        Serial.printf("  F%d:%d%d%d R%d:%d%d%d\n",
            i, fwd[i][0], fwd[i][1], fwd[i][2],
            i, rev[i][0], rev[i][1], rev[i][2]);
    }
#endif

    int base = -1;
    bool npn = true;
    for (uint8_t i = 0; i < 3; i++) {
        int f = 0, r = 0;
        for (uint8_t j = 0; j < 3; j++) {
            if (i == j) continue;
            if (fwd[i][j]) f++;
            if (rev[j][i]) r++;
        }
        if (f == 2) { base = i; npn = true; break; }
        if (r == 2) { base = i; npn = false; break; }
    }

#ifdef DEBUG_IDENTIFY
    Serial.printf("BASE=%d npn=%d\n", base, npn);
#endif
    if (base < 0) return r;

    r.type = npn ? TransistorType::NPN : TransistorType::PNP;

    uint8_t other[2], oi = 0;
    for (uint8_t j = 0; j < 3; j++) {
        if (j != (uint8_t)base) other[oi++] = j;
    }

    float ie0 = 0, ie1 = 0;
    float h0 = measureHfe(other[0], base, other[1], npn, &ie0);
    float h1 = measureHfe(other[1], base, other[0], npn, &ie1);
#ifdef DEBUG_IDENTIFY
    Serial.printf("h0=%.1f h1=%.1f npn=%d\n", h0, h1, npn);
#endif

    if (h0 >= h1 && h0 >= MIN_HFE) {
        r.pins = {other[0], (uint8_t)base, other[1]};
        r.hfe = h0;
        r.i_e = ie0;
    } else if (h1 >= MIN_HFE) {
        r.pins = {other[1], (uint8_t)base, other[0]};
        r.hfe = h1;
        r.i_e = ie1;
    } else {
        r.type = TransistorType::UNKNOWN;
        return r;
    }

    if (npn) {
        tp.setPin(r.pins.emitter, PinDrive::DRV_LOW, PinDrive::DRV_HIZ);
        tp.setPin(r.pins.base, PinDrive::DRV_HIGH, PinDrive::DRV_HIZ);
    } else {
        tp.setPin(r.pins.emitter, PinDrive::DRV_HIGH, PinDrive::DRV_HIZ);
        tp.setPin(r.pins.base, PinDrive::DRV_LOW, PinDrive::DRV_HIZ);
    }
    delay(10);
    float vB = readV(r.pins.base);
    float vE = readV(r.pins.emitter);
    r.v_be = npn ? (vB - vE) : (vE - vB);
    tp.allInput();

    return r;
}

const char* Identifier::typeString(TransistorType t) {
    switch (t) {
        case TransistorType::NPN: return "NPN";
        case TransistorType::PNP: return "PNP";
        default: return "UNKNOWN";
    }
}
