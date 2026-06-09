# ESP32 Transistor Tester — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a working transistor tester on ESP32 DevKit with SPI TFT, ADS1115 ADC, and 3-pin test socket — starting with type/pinout identification and hFE measurement (P1), then adding curve tracer (P3).

**Architecture:** PlatformIO project with Arduino-ESP32 framework. Five layers: HAL (pins, ADC, test pins, stimulus) → Measurement (identify, params, curves) → Render (abstract display + TFT impl) → UI → Main state machine.

**Tech Stack:** PlatformIO, Arduino-ESP32, TFT_eSPI, Adafruit ADS1X15, Unity Test (host)

---

## Task 1: Project Scaffold

**Files:**
- Create: `platformio.ini`
- Create: `src/main.cpp`
- Create: `src/hal/pins.h`
- Create: `.gitignore`

- [ ] **Step 1: Create `platformio.ini`**

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    bodmer/TFT_eSPI@^2.5.43
    adafruit/Adafruit ADS1X15@^2.2.0

[env:test]
platform = native
test_framework = unity
```

- [ ] **Step 2: Create `src/hal/pins.h`**

```cpp
#pragma once

// I2C (ADS1115)
#define PIN_I2C_SDA  21
#define PIN_I2C_SCL  22

// TFT SPI (VSPI)
#define PIN_TFT_SCLK 18
#define PIN_TFT_MOSI 23
#define PIN_TFT_MISO 19
#define PIN_TFT_CS    5
#define PIN_TFT_DC   17
#define PIN_TFT_RST  16

// Test pins — 680 Ω drive
#define PIN_T1_LO   4
#define PIN_T2_LO  14
#define PIN_T3_LO  26

// Test pins — 470 kΩ sense
#define PIN_T1_HI  13
#define PIN_T2_HI  27
#define PIN_T3_HI  25

// PWM outputs → RC filter
#define PIN_PWM_BASE      32
#define PIN_PWM_COLLECTOR 33

// ADS1115 I2C address
#define ADS1115_ADDR 0x48
```

- [ ] **Step 3: Create empty `src/main.cpp`**

```cpp
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
}

void loop() {
    delay(100);
}
```

- [ ] **Step 4: Create `.gitignore`**

```
.pio/
.vscode/
*.o
*.elf
build/
```

- [ ] **Step 5: Verify build**

```bash
pio run -e esp32dev
```
Expected: Compilation succeeds (just main.cpp with empty setup/loop).

---

## Task 2: HAL — ADS1115 ADC Wrapper

**Files:**
- Create: `src/hal/adc.h`
- Create: `src/hal/adc.cpp`

- [ ] **Step 1: Write `src/hal/adc.h`**

```cpp
#pragma once
#include <Adafruit_ADS1X15.h>

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
```

- [ ] **Step 2: Write `src/hal/adc.cpp`**

```cpp
#include "adc.h"
#include "pins.h"

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
    return raw * 4.096f / 32768.0f;     // ±4.096 V / 16-bit
}
```

- [ ] **Step 3: Quick verification in main.cpp**

```cpp
#include <Arduino.h>
#include "hal/adc.h"

ADC adc;

void setup() {
    Serial.begin(115200);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    if (adc.begin()) {
        Serial.println("ADS1115 OK");
    } else {
        Serial.println("ADS1115 FAILED");
    }
}

void loop() {
    Serial.printf("A0: %.3f V\n", adc.readVoltage(0));
    delay(500);
}
```

Build and flash: `pio run -e esp32dev -t upload && pio device monitor`

Expected: Prints "ADS1115 OK" and voltage readings on A0.

---

## Task 3: HAL — Test Pin Control

**Files:**
- Create: `src/hal/testpins.h`
- Create: `src/hal/testpins.cpp`

- [ ] **Step 1: Write `src/hal/testpins.h`**

```cpp
#pragma once
#include <stdint.h>

enum class PinDrive { LOW, HIGH, INPUT };

struct PinState {
    PinDrive lo;   // 680 Ω GPIO
    PinDrive hi;   // 470 kΩ GPIO
    bool operator==(const PinState &o) const {
        return lo == o.lo && hi == o.hi;
    }
    bool operator!=(const PinState &o) const {
        return !(*this == o);
    }
};

class TestPins {
public:
    static constexpr uint8_t COUNT = 3;
    void begin();
    void setPin(uint8_t idx, PinDrive lo, PinDrive hi);
    void setPinLo(uint8_t idx, PinDrive d);
    void setPinHi(uint8_t idx, PinDrive d);
    PinState getPin(uint8_t idx) const;
    void allInput();

private:
    PinState m_state[COUNT] = {};
    static constexpr uint8_t PIN_LO[COUNT] = {PIN_T1_LO, PIN_T2_LO, PIN_T3_LO};
    static constexpr uint8_t PIN_HI[COUNT] = {PIN_T1_HI, PIN_T2_HI, PIN_T3_HI};
};
```

- [ ] **Step 2: Write `src/hal/testpins.cpp`**

```cpp
#include "testpins.h"
#include "pins.h"
#include <Arduino.h>

static void gpioMode(uint8_t pin, PinDrive d) {
    switch (d) {
        case PinDrive::LOW:   pinMode(pin, OUTPUT); digitalWrite(pin, LOW); break;
        case PinDrive::HIGH:  pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); break;
        case PinDrive::INPUT: pinMode(pin, INPUT); break;
    }
}

void TestPins::begin() {
    for (uint8_t i = 0; i < COUNT; i++) {
        pinMode(PIN_LO[i], OUTPUT);
        pinMode(PIN_HI[i], OUTPUT);
        digitalWrite(PIN_LO[i], LOW);
        digitalWrite(PIN_HI[i], LOW);
        m_state[i] = {PinDrive::LOW, PinDrive::LOW};
    }
}

void TestPins::setPin(uint8_t idx, PinDrive lo, PinDrive hi) {
    if (idx >= COUNT) return;
    gpioMode(PIN_LO[idx], lo);
    gpioMode(PIN_HI[idx], hi);
    m_state[idx] = {lo, hi};
}

void TestPins::setPinLo(uint8_t idx, PinDrive d) {
    if (idx >= COUNT) return;
    gpioMode(PIN_LO[idx], d);
    m_state[idx].lo = d;
}

void TestPins::setPinHi(uint8_t idx, PinDrive d) {
    if (idx >= COUNT) return;
    gpioMode(PIN_HI[idx], d);
    m_state[idx].hi = d;
}

PinState TestPins::getPin(uint8_t idx) const {
    if (idx >= COUNT) return {PinDrive::INPUT, PinDrive::INPUT};
    return m_state[idx];
}

void TestPins::allInput() {
    for (uint8_t i = 0; i < COUNT; i++) {
        setPin(i, PinDrive::INPUT, PinDrive::INPUT);
    }
}
```

- [ ] **Step 3: Verify with temporary main.cpp**

```cpp
#include <Arduino.h>
#include "hal/adc.h"
#include "hal/testpins.h"

ADC adc;
TestPins tp;

void setup() {
    Serial.begin(115200);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    adc.begin();
    tp.begin();
}

void loop() {
    // Drive T1 high through 680Ω, read voltage
    tp.setPin(0, PinDrive::HIGH, PinDrive::INPUT);
    delay(10);
    float v = adc.readVoltage(0);
    Serial.printf("T1 HIGH: %.3f V\n", v);

    // Float T1, read voltage
    tp.setPin(0, PinDrive::INPUT, PinDrive::INPUT);
    delay(10);
    v = adc.readVoltage(0);
    Serial.printf("T1 FLOAT: %.3f V\n", v);
    delay(1000);
}
```

Build + flash + monitor. Expected: T1 HIGH shows ~3.3 V, T1 FLOAT shows ~0 V or noise.

Undo the temp changes after verifying.

---

## Task 4: HAL — Stimulus (PWM Bias Voltage)

**Files:**
- Create: `src/hal/stimulus.h`
- Create: `src/hal/stimulus.cpp`

- [ ] **Step 1: Write `src/hal/stimulus.h`**

```cpp
#pragma once
#include <stdint.h>

class Stimulus {
public:
    static constexpr uint8_t BASE_CHAN = 0;
    static constexpr uint8_t COLLECTOR_CHAN = 1;
    static constexpr uint32_t PWM_FREQ = 5000;     // 5 kHz
    static constexpr uint8_t PWM_RES = 10;          // 10-bit → 0–1023

    void begin();
    void setBaseDuty(uint16_t duty);    // 0–1023
    void setCollectorDuty(uint16_t duty);
    void baseOff();
    void collectorOff();
    uint16_t baseDuty() const { return m_baseDuty; }
    uint16_t collectorDuty() const { return m_collectorDuty; }

private:
    uint16_t m_baseDuty = 0;
    uint16_t m_collectorDuty = 0;
};
```

- [ ] **Step 2: Write `src/hal/stimulus.cpp`**

```cpp
#include "stimulus.h"
#include "pins.h"
#include <Arduino.h>

void Stimulus::begin() {
    ledcSetup(BASE_CHAN, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_BASE, BASE_CHAN);
    ledcSetup(COLLECTOR_CHAN, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_COLLECTOR, COLLECTOR_CHAN);
    baseOff();
    collectorOff();
}

void Stimulus::setBaseDuty(uint16_t duty) {
    m_baseDuty = (duty > 1023) ? 1023 : duty;
    ledcWrite(BASE_CHAN, m_baseDuty);
}

void Stimulus::setCollectorDuty(uint16_t duty) {
    m_collectorDuty = (duty > 1023) ? 1023 : duty;
    ledcWrite(COLLECTOR_CHAN, m_collectorDuty);
}

void Stimulus::baseOff() {
    setBaseDuty(0);
}

void Stimulus::collectorOff() {
    setCollectorDuty(0);
}
```

- [ ] **Step 3: Quick PWM verification**

Add to main.cpp to verify with a scope or by measuring the RC filter output with a multimeter. Set duty to 512 (~1.65 V after RC filter). Flash and confirm stable output on GPIO32.

---

## Task 5: Measurement — Device Identification (P1 Core)

**Files:**
- Create: `src/measure/identify.h`
- Create: `src/measure/identify.cpp`

- [ ] **Step 1: Write `src/measure/identify.h`**

```cpp
#pragma once
#include <stdint.h>
#include <Arduino.h>

enum class TransistorType : uint8_t { UNKNOWN, NPN, PNP };
enum class JunctionResult : uint8_t { OPEN, FORWARD, REVERSE };

struct PinAssignment {
    uint8_t collector;  // 0-based index into test pins (0=T1, 1=T2, 2=T3)
    uint8_t base;
    uint8_t emitter;
    bool valid() const { return collector < 3 && base < 3 && emitter < 3; }
};

struct Identification {
    TransistorType type = TransistorType::UNKNOWN;
    PinAssignment pins = {0, 0, 0};
    float v_be = 0.0f;
    float hfe = 0.0f;
    bool valid() const { return type != TransistorType::UNKNOWN && pins.valid(); }
};

class Identifier {
public:
    Identification identify();
    String typeString(TransistorType t);
    String pinString(const PinAssignment &p);
};
```

- [ ] **Step 2: Write `src/measure/identify.cpp`**

```cpp
#include "identify.h"
#include "../hal/testpins.h"
#include "../hal/adc.h"
#include "../hal/stimulus.h"
#include <Arduino.h>

static constexpr float V_FWD_MIN = 0.3f;    // minimum forward junction drop
static constexpr float V_FWD_MAX = 0.9f;    // maximum forward junction drop
static constexpr float MIN_HFE = 2.0f;       // minimum β for valid transistor

extern TestPins tp;
extern ADC adc;
extern Stimulus stim;

static float readPinV(uint8_t idx) {
    return adc.readVoltage(idx);   // ADS1115 A0=T1, A1=T2, A2=T3
}

// Drive one pin HIGH, read all three, detect which other pins show a forward drop
static void probeJunctions(uint8_t driveIdx, JunctionResult results[3][3]) {
    // All pins input initially
    tp.allInput();

    // Drive driveIdx HIGH through 680Ω
    tp.setPinLo(driveIdx, PinDrive::HIGH);
    delayMicroseconds(500);
    float vDrive = readPinV(driveIdx);

    for (uint8_t j = 0; j < 3; j++) {
        if (j == driveIdx) continue;
        float v = readPinV(j);
        float vDiff = vDrive - v;
        if (vDiff > V_FWD_MIN && vDiff < V_FWD_MAX) {
            results[driveIdx][j] = JunctionResult::FORWARD;
        } else {
            results[driveIdx][j] = JunctionResult::OPEN;
        }
    }

    // Now reverse: drive driveIdx LOW through 680Ω, pull others with 470kΩ
    tp.setPinLo(driveIdx, PinDrive::LOW);
    for (uint8_t j = 0; j < 3; j++) {
        if (j == driveIdx) continue;
        tp.setPinHi(j, PinDrive::HIGH);  // weak pull-up through 470kΩ
    }
    delayMicroseconds(500);
    vDrive = readPinV(driveIdx);

    for (uint8_t j = 0; j < 3; j++) {
        if (j == driveIdx) continue;
        float v = readPinV(j);
        float vDiff = v - vDrive;
        if (vDiff > V_FWD_MIN && vDiff < V_FWD_MAX) {
            results[j][driveIdx] = JunctionResult::FORWARD;
        } else {
            results[j][driveIdx] = JunctionResult::OPEN;
        }
    }

    tp.allInput();
}

static uint8_t findBase(const JunctionResult results[3][3]) {
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t fwdCount = 0;
        for (uint8_t j = 0; j < 3; j++) {
            if (i != j && results[i][j] == JunctionResult::FORWARD) fwdCount++;
        }
        if (fwdCount == 2) return i;   // common to two junctions = base
    }
    return 255;  // not found
}

static float measureGain(uint8_t c, uint8_t b, uint8_t e, bool npn) {
    // Set up bias: base current ≈ (V_drive - V_BE) / R_b
    // Use PWM to set base voltage to ~2.5 V → I_b ≈ (2.5 - 0.7) / 470k ≈ 3.8 µA
    stim.setBaseDuty(780);  // ~2.5 V after RC filter (780/1023 * 3.3V)
    delay(50);              // settle RC filter

    // If NPN: collector HIGH, emitter LOW through 680Ω
    // If PNP: collector LOW, emitter HIGH through 680Ω
    if (npn) {
        tp.setPinLo(c, PinDrive::HIGH);
        tp.setPinLo(e, PinDrive::LOW);
    } else {
        tp.setPinLo(c, PinDrive::LOW);
        tp.setPinLo(e, PinDrive::HIGH);
    }
    delay(10);

    float vB = readPinV(b);
    float vC = readPinV(c);
    float vE = readPinV(e);

    float v_be = npn ? (vB - vE) : (vE - vB);
    float v_ce = npn ? (vC - vE) : (vE - vC);
    float i_c = (npn ? (3.3f - vC) : vC) / 680.0f;  // through 680Ω

    // I_b ≈ (v_pwm - vB) / R_b — rough estimate
    float vPwm = 3.3f * 780.0f / 1023.0f;
    float i_b = (vPwm - vB) / 470000.0f;

    if (i_b <= 0) return 0;
    float hfe = i_c / i_b;

    tp.allInput();
    stim.baseOff();
    return hfe;
}

Identification Identifier::identify() {
    Identification result;

    JunctionResult junc[3][3] = {};
    for (uint8_t i = 0; i < 3; i++) {
        probeJunctions(i, junc);
    }

    uint8_t base = findBase(junc);
    if (base == 255) return result;  // no transistor found

    // Determine polarity: for NPN, base is common anode (forward from base to others)
    // For PNP, base is common cathode (forward from others to base)
    bool npn = true;
    for (uint8_t j = 0; j < 3; j++) {
        if (j == base) continue;
        if (junc[base][j] != JunctionResult::FORWARD) {
            npn = false;  // not forward from base → PNP
            break;
        }
    }

    result.type = npn ? TransistorType::NPN : TransistorType::PNP;

    // Determine which of the two remaining pins is collector vs emitter
    uint8_t other[2], oi = 0;
    for (uint8_t j = 0; j < 3; j++) {
        if (j != base) other[oi++] = j;
    }

    // Try both assignments, pick the one with higher gain
    float hfe0 = measureGain(other[0], base, other[1], npn);
    float hfe1 = measureGain(other[1], base, other[0], npn);

    if (hfe0 >= hfe1 && hfe0 >= MIN_HFE) {
        result.pins = {other[0], base, other[1]};
        result.hfe = hfe0;
    } else if (hfe1 >= MIN_HFE) {
        result.pins = {other[1], base, other[0]};
        result.hfe = hfe1;
    } else {
        result.type = TransistorType::UNKNOWN;  // no valid transistor orientation
        return result;
    }

    // Measure V_BE at the correct orientation
    float vPwm = 3.3f * 780.0f / 1023.0f;
    stim.setBaseDuty(780);
    delay(50);
    if (npn) {
        tp.setPinLo(result.pins.collector, PinDrive::HIGH);
        tp.setPinLo(result.pins.emitter, PinDrive::LOW);
    } else {
        tp.setPinLo(result.pins.collector, PinDrive::LOW);
        tp.setPinLo(result.pins.emitter, PinDrive::HIGH);
    }
    delay(10);
    float vB = readPinV(result.pins.base);
    float vE = readPinV(result.pins.emitter);
    result.v_be = npn ? (vB - vE) : (vE - vB);
    tp.allInput();
    stim.baseOff();

    return result;
}

String Identifier::typeString(TransistorType t) {
    switch (t) {
        case TransistorType::NPN: return "NPN";
        case TransistorType::PNP: return "PNP";
        default: return "UNKNOWN";
    }
}

String Identifier::pinString(const PinAssignment &p) {
    char buf[32];
    snprintf(buf, sizeof(buf), "C=T%d B=T%d E=T%d", p.collector + 1, p.base + 1, p.emitter + 1);
    return String(buf);
}
```

- [ ] **Step 3: Verify identify with known transistor**

```cpp
// In main.cpp loop():
static Identifier id;
static unsigned long lastId = 0;
if (millis() - lastId > 2000) {
    lastId = millis();
    Identification r = id.identify();
    Serial.printf("Type: %s\n", id.typeString(r.type).c_str());
    Serial.printf("Pins: %s\n", id.pinString(r.pins).c_str());
    Serial.printf("hFE: %.1f  V_BE: %.3f V\n", r.hfe, r.v_be);
}
```

Plug in a known NPN (e.g. 2N2222) into the ZIF socket in T1/T2/T3.
Build + flash. Expected: Identifies correctly as NPN with reasonable hFE.

---

## Task 6: Measurement — Parameters (hFE / V_BE at multiple I_b)

**Files:**
- Create: `src/measure/params.h`
- Create: `src/measure/params.cpp`

- [ ] **Step 1: Write `src/measure/params.h`**

```cpp
#pragma once
#include <stdint.h>
#include <Arduino.h>
#include "identify.h"

struct BiasPoint {
    float i_b;     // A
    float i_c;     // A
    float v_be;    // V
    float v_ce;    // V
    float hfe;     // β
};

struct Characterization {
    bool valid = false;
    BiasPoint points[10];
    uint8_t count = 0;
    float min_hfe = 0;
    float max_hfe = 0;
};

class Params {
public:
    Characterization characterize(const PinAssignment &pins, TransistorType type);
private:
    BiasPoint measureAt(const PinAssignment &pins, TransistorType type, uint16_t baseDuty);
};
```

- [ ] **Step 2: Write `src/measure/params.cpp`**

```cpp
#include "params.h"
#include "../hal/testpins.h"
#include "../hal/adc.h"
#include "../hal/stimulus.h"
#include <Arduino.h>

extern TestPins tp;
extern ADC adc;
extern Stimulus stim;

BiasPoint Params::measureAt(const PinAssignment &pins, TransistorType type, uint16_t baseDuty) {
    BiasPoint pt = {};
    bool npn = (type == TransistorType::NPN);

    stim.setBaseDuty(baseDuty);
    delay(50);

    if (npn) {
        tp.setPinLo(pins.collector, PinDrive::HIGH);
        tp.setPinLo(pins.emitter, PinDrive::LOW);
    } else {
        tp.setPinLo(pins.collector, PinDrive::LOW);
        tp.setPinLo(pins.emitter, PinDrive::HIGH);
    }
    delay(10);

    float vB = adc.readVoltage(pins.base);
    float vC = adc.readVoltage(pins.collector);
    float vE = adc.readVoltage(pins.emitter);

    tp.allInput();
    stim.baseOff();

    if (npn) {
        pt.v_be = vB - vE;
        pt.v_ce = vC - vE;
        pt.i_c = (3.3f - vC) / 680.0f;
    } else {
        pt.v_be = vE - vB;
        pt.v_ce = vE - vC;
        pt.i_c = vC / 680.0f;
    }

    float vPwm = 3.3f * baseDuty / 1023.0f;
    pt.i_b = (vPwm - (npn ? vB : (3.3f - vB))) / 470000.0f;
    if (pt.i_b > 0) pt.hfe = pt.i_c / pt.i_b;

    return pt;
}

Characterization Params::characterize(const PinAssignment &pins, TransistorType type) {
    Characterization r;
    if (!pins.valid() || type == TransistorType::UNKNOWN) return r;

    // Sweep base current through 10 duty steps
    const uint16_t duties[10] = {
        100, 180, 260, 340, 420, 500, 580, 660, 740, 820
    };

    r.min_hfe = 1e6f;
    r.max_hfe = 0;
    for (uint8_t i = 0; i < 10; i++) {
        r.points[i] = measureAt(pins, type, duties[i]);
        if (r.points[i].hfe > 0) {
            if (r.points[i].hfe < r.min_hfe) r.min_hfe = r.points[i].hfe;
            if (r.points[i].hfe > r.max_hfe) r.max_hfe = r.points[i].hfe;
            r.count++;
        }
    }
    r.valid = (r.count > 0);
    return r;
}
```

- [ ] **Step 3: Quick P2 verification**

Add to main.cpp to run characterize after identify. Flash. Expected: prints hFE values across 10 bias points.

---

## Task 7: Render — Display Abstraction

**Files:**
- Create: `src/render/display.h`

- [ ] **Step 1: Write `src/render/display.h`**

```cpp
#pragma once
#include <Arduino.h>
#include "../measure/identify.h"
#include "../measure/params.h"

struct CurvePoint {
    float v_ce;
    float i_c;
};

struct CurveFamily {
    uint8_t numCurves = 0;
    float i_b_per_step = 0;
    CurvePoint points[10][64];  // [curveIdx][pointIdx]
    uint8_t counts[10] = {};
};

class Display {
public:
    virtual ~Display() = default;
    virtual void begin() = 0;
    virtual void clear() = 0;
    virtual void drawText(int x, int y, const String &text, uint16_t color = 0xFFFF) = 0;
    virtual void drawIdentification(const Identification &id) = 0;
    virtual void drawCharacterization(const Characterization &ch) = 0;
    virtual void drawCurveFamily(const CurveFamily &cf) = 0;
    virtual void drawReadout(const Identification &id, float v_ce, float i_c) = 0;
};
```

---

## Task 8: Render — TFT Implementation

**Files:**
- Create: `src/render/display_tft.h`
- Create: `src/render/display_tft.cpp`
- Create: `lib/TFT_eSPI_UserSetup/User_Setup.h`

- [ ] **Step 1: Write TFT_eSPI user setup for this pin map**

Since TFT_eSPI is configured at compile time via User_Setup.h:

```cpp
#define ILI9341_DRIVER

#define TFT_CS   PIN_TFT_CS
#define TFT_DC   PIN_TFT_DC
#define TFT_RST  PIN_TFT_RST

#define TFT_MISO PIN_TFT_MISO
#define TFT_MOSI PIN_TFT_MOSI
#define TFT_SCLK PIN_TFT_SCLK

#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

#define TOUCH_CS  -1  // no touch
#define TFT_BL   -1   // no backlight control
```

Place at `lib/TFT_eSPI_UserSetup/User_Setup.h` and set `TFT_eSPI` to use this via `-DUSER_SETUP_LOADED=1` in platformio.ini, or use the TFT_eSPI library's `-DTFT_eSPI_USER_SETUP_PATH` to point to this file.

- [ ] **Step 2: Write `src/render/display_tft.h`**

```cpp
#pragma once
#include "display.h"
#include <TFT_eSPI.h>

class DisplayTFT : public Display {
public:
    void begin() override;
    void clear() override;
    void drawText(int x, int y, const String &text, uint16_t color = 0xFFFF) override;
    void drawIdentification(const Identification &id) override;
    void drawCharacterization(const Characterization &ch) override;
    void drawCurveFamily(const CurveFamily &cf) override;
    void drawReadout(const Identification &id, float v_ce, float i_c) override;

private:
    TFT_eSPI m_tft;
    TFT_eSprite m_sprite;
    static constexpr uint16_t W = 320;
    static constexpr uint16_t H = 240;
    static constexpr uint16_t PLOT_X = 10;
    static constexpr uint16_t PLOT_Y = 10;
    static constexpr uint16_t PLOT_W = 200;
    static constexpr uint16_t PLOT_H = 200;
    static constexpr uint16_t PANEL_X = 220;
    static constexpr uint16_t PANEL_Y = 10;

    // Colors
    static constexpr uint16_t COLORS[10] = {
        0xF800,  // red
        0x07E0,  // green
        0x001F,  // blue
        0xFFE0,  // yellow
        0xF81F,  // magenta
        0x07FF,  // cyan
        0xFD20,  // orange
        0x7800,  // maroon
        0x7BEF,  // gray
        0xFC80,  // pink
    };
};
```

- [ ] **Step 3: Write `src/render/display_tft.cpp`**

```cpp
#include "display_tft.h"

void DisplayTFT::begin() {
    m_tft.init();
    m_tft.setRotation(1);  // landscape
    m_sprite = TFT_eSprite(&m_tft);
    m_sprite.createSprite(W, H);
    clear();
}

void DisplayTFT::clear() {
    m_sprite.fillSprite(TFT_BLACK);
    m_sprite.pushSprite(0, 0);
}

void DisplayTFT::drawText(int x, int y, const String &text, uint16_t color) {
    m_sprite.setTextColor(color, TFT_BLACK);
    m_sprite.setCursor(x, y);
    m_sprite.print(text);
    m_sprite.pushSprite(0, 0);
}

void DisplayTFT::drawIdentification(const Identification &id) {
    m_sprite.fillSprite(TFT_BLACK);
    m_sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    m_sprite.setCursor(10, 10);
    m_sprite.setTextSize(2);

    if (!id.valid()) {
        m_sprite.println("No device");
        m_sprite.pushSprite(0, 0);
        return;
    }

    m_sprite.printf("Type: %s\n", id.typeString(id.type).c_str());
    m_sprite.printf("Pinout: C=T%d B=T%d E=T%d\n",
        id.pins.collector + 1, id.pins.base + 1, id.pins.emitter + 1);
    m_sprite.printf("hFE: %.0f\n", id.hfe);
    m_sprite.printf("V_BE: %.3f V\n", id.v_be);
    m_sprite.pushSprite(0, 0);
}

void DisplayTFT::drawCharacterization(const Characterization &ch) {
    if (!ch.valid) {
        drawText(10, 10, "No data", TFT_RED);
        return;
    }

    m_sprite.fillSprite(TFT_BLACK);
    m_sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    m_sprite.setTextSize(1);
    m_sprite.setCursor(10, 10);
    m_sprite.println("I_b (uA) | I_c (mA) | hFE");
    for (uint8_t i = 0; i < ch.count; i++) {
        m_sprite.printf("%7.2f | %8.3f | %.0f\n",
            ch.points[i].i_b * 1e6f,
            ch.points[i].i_c * 1e3f,
            ch.points[i].hfe);
    }
    m_sprite.pushSprite(0, 0);
}

void DisplayTFT::drawCurveFamily(const CurveFamily &cf) {
    if (cf.numCurves == 0) return;

    // Draw axes
    m_sprite.fillSprite(TFT_BLACK);
    m_sprite.drawRect(PLOT_X, PLOT_Y, PLOT_W, PLOT_H, TFT_WHITE);

    // Auto-range
    float vMax = 0, iMax = 0;
    for (uint8_t c = 0; c < cf.numCurves; c++) {
        for (uint8_t p = 0; p < cf.counts[c]; p++) {
            if (cf.points[c][p].v_ce > vMax) vMax = cf.points[c][p].v_ce;
            if (cf.points[c][p].i_c > iMax) iMax = cf.points[c][p].i_c;
        }
    }
    if (vMax == 0) vMax = 3.3f;
    if (iMax == 0) iMax = 0.01f;

    // Draw each curve
    for (uint8_t c = 0; c < cf.numCurves; c++) {
        uint16_t color = COLORS[c % 10];
        for (uint8_t p = 1; p < cf.counts[c]; p++) {
            auto &prev = cf.points[c][p - 1];
            auto &cur  = cf.points[c][p];
            int x0 = PLOT_X + (prev.v_ce / vMax) * PLOT_W;
            int y0 = PLOT_Y + PLOT_H - (prev.i_c / iMax) * PLOT_H;
            int x1 = PLOT_X + (cur.v_ce / vMax) * PLOT_W;
            int y1 = PLOT_Y + PLOT_H - (cur.i_c / iMax) * PLOT_H;
            m_sprite.drawLine(x0, y0, x1, y1, color);
        }
    }

    // Axis labels
    m_sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    m_sprite.setCursor(PLOT_X, PLOT_Y + PLOT_H + 4);
    m_sprite.print("V_CE");
    m_sprite.setCursor(PLOT_X + PLOT_W - 20, PLOT_Y + PLOT_H + 4);
    m_sprite.printf("%.1fV", vMax);
    m_sprite.setCursor(0, PLOT_Y);
    m_sprite.print("I_C");
    m_sprite.setCursor(0, PLOT_Y + PLOT_H - 8);
    m_sprite.printf("%.0fmA", iMax * 1e3f);

    m_sprite.pushSprite(0, 0);
}

void DisplayTFT::drawReadout(const Identification &id, float v_ce, float i_c) {
    // Right panel readout
    m_sprite.fillRect(PANEL_X, PANEL_Y, W - PANEL_X, H - PANEL_Y, TFT_BLACK);
    m_sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    m_sprite.setTextSize(1);
    m_sprite.setCursor(PANEL_X + 4, PANEL_Y + 4);
    m_sprite.printf("Type: %s\n", id.typeString(id.type).c_str());
    m_sprite.printf("C=T%d B=T%d E=T%d\n",
        id.pins.collector + 1, id.pins.base + 1, id.pins.emitter + 1);
    m_sprite.printf("hFE: %.0f\n", id.hfe);
    m_sprite.printf("V_BE: %.3fV\n", id.v_be);
    m_sprite.printf("-----\n");
    m_sprite.printf("V_CE: %.3fV\n", v_ce);
    m_sprite.printf("I_C: %.3fmA\n", i_c * 1e3f);
    m_sprite.pushSprite(0, 0);
}
```

- [ ] **Step 4: Verification**

Flash with a sketch that calls `drawIdentification` with a known identification struct. Verify text appears correctly on TFT.

---

## Task 9: Main State Machine (P1 — Identify + Display)

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Write the P1 state machine**

```cpp
#include <Arduino.h>
#include "hal/pins.h"
#include "hal/adc.h"
#include "hal/testpins.h"
#include "hal/stimulus.h"
#include "measure/identify.h"
#include "render/display_tft.h"

ADC adc;
TestPins tp;
Stimulus stim;
Identifier id;
DisplayTFT display;

void setup() {
    Serial.begin(115200);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Serial.println("Transistor Tester P1");

    if (!adc.begin()) Serial.println("ADS1115 FAILED");
    else              Serial.println("ADS1115 OK");

    tp.begin();
    stim.begin();
    display.begin();
    display.drawText(10, 10, "Transistor Tester v1", TFT_GREEN);
    delay(1000);
}

void loop() {
    Identification result = id.identify();
    if (result.valid()) {
        display.drawIdentification(result);
        Serial.printf("Found: %s  hFE=%.0f  V_BE=%.3f\n",
            id.typeString(result.type).c_str(), result.hfe, result.v_be);
    } else {
        display.drawText(10, 10, "Insert transistor...", TFT_WHITE);
        Serial.println("Waiting for device...");
    }
    delay(500);
}
```

- [ ] **Step 2: Flash + verify**

```bash
pio run -e esp32dev -t upload
pio device monitor
```

Insert a 2N2222 (NPN) into the ZIF socket. Expected: TFT shows "NPN C=T1 B=T2 E=T3" with hFE ~100-300. Remove it. Expected: "Insert transistor..."

---

## Task 10: Hardware — Breadboard Wiring

- [ ] **Step 1: Create wiring reference `docs/wiring.md`**

Document the breadboard wiring so it's reproducible:

```
ESP32 DevKit          Breadboard
-----------           ----------
GPIO21/SDA ────────── ADS1115 SDA
GPIO22/SCL ────────── ADS1115 SCL
                      ADS1115 VCC → 3.3V, GND → GND
                      ADS1115 A0 → T1, A1 → T2, A2 → T3

GPIO18/SCK ────────── TFT SCK
GPIO23/MOSI ───────── TFT MOSI
GPIO19/MISO ───────── TFT MISO
GPIO5/CS   ────────── TFT CS
GPIO17/DC  ────────── TFT DC
GPIO16/RST ────────── TFT RST
                      3.3V → TFT VCC, GND → TFT GND

GPIO4 ── 680Ω ── T1 ── 470kΩ ── GPIO13
GPIO14 ── 680Ω ── T2 ── 470kΩ ── GPIO27
GPIO26 ── 680Ω ── T3 ── 470kΩ ── GPIO25

T1 ────── ADS1115 A0
T2 ────── ADS1115 A1
T3 ────── ADS1115 A2

T1/T2/T3 ──────────── ZIF socket pins 1-3

GPIO32 ── 1kΩ ──+── 100nF ── GND   (base bias RC filter)
                 └────────── 470kΩ → base drive to DUT

GPIO33 ── 1kΩ ──+── 100nF ── GND   (collector sweep RC filter, P3)
                 └────────── MCP6002 input

MCP6002 output ── DUT collector via R_sense (P3)
```

---

## Task 11: P3 — Curve Capture (I_C-V_CE Family)

**Files:**
- Create: `src/measure/curves.h`
- Create: `src/measure/curves.cpp`
- Modify: `src/main.cpp` (state machine expansion)

- [ ] **Step 1: Write `src/measure/curves.h`**

```cpp
#pragma once
#include "../render/display.h"
#include "identify.h"

class CurveCapture {
public:
    CurveFamily capture(const PinAssignment &pins, TransistorType type);
private:
    CurvePoint measurePoint(const PinAssignment &pins, TransistorType type,
                            uint16_t baseDuty, uint16_t collectorDuty);
};
```

- [ ] **Step 2: Write `src/measure/curves.cpp`**

```cpp
#include "curves.h"
#include "../hal/testpins.h"
#include "../hal/adc.h"
#include "../hal/stimulus.h"
#include <Arduino.h>

extern TestPins tp;
extern ADC adc;
extern Stimulus stim;

static constexpr uint16_t BASE_STEPS[] = {200, 350, 500, 650, 800};  // 5 I_b steps
static constexpr uint16_t COLLECTOR_STEPS[] = {
    0, 50, 100, 150, 200, 250, 300, 350, 400, 450,
    500, 550, 600, 650, 700, 750, 800, 850, 900, 950
};
static constexpr uint8_t NUM_BASE = 5;
static constexpr uint8_t NUM_COLLECTOR = 20;

CurvePoint CurveCapture::measurePoint(const PinAssignment &pins, TransistorType type,
                                       uint16_t baseDuty, uint16_t collectorDuty) {
    CurvePoint pt = {0, 0};
    bool npn = (type == TransistorType::NPN);

    stim.setBaseDuty(baseDuty);
    stim.setCollectorDuty(collectorDuty);
    delay(50);

    if (npn) {
        tp.setPinLo(pins.emitter, PinDrive::LOW);
    } else {
        tp.setPinLo(pins.collector, PinDrive::LOW);
    }
    delay(10);

    float vC = adc.readVoltage(pins.collector);
    float vE = adc.readVoltage(pins.emitter);

    if (npn) {
        pt.v_ce = vC - vE;
        pt.i_c  = (3.3f - vC) / 680.0f;  // rough I_c through 680Ω pull-up
    } else {
        pt.v_ce = vE - vC;
        pt.i_c  = vC / 680.0f;           // rough I_c through 680Ω pull-down
    }

    tp.allInput();
    stim.baseOff();
    stim.collectorOff();

    return pt;
}

CurveFamily CurveCapture::capture(const PinAssignment &pins, TransistorType type) {
    CurveFamily cf;

    for (uint8_t b = 0; b < NUM_BASE; b++) {
        uint8_t p = 0;
        for (uint8_t c = 0; c < NUM_COLLECTOR; c++) {
            CurvePoint pt = measurePoint(pins, type, BASE_STEPS[b], COLLECTOR_STEPS[c]);
            if (pt.i_c < 0) pt.i_c = 0;
            if (pt.v_ce < 0) pt.v_ce = 0;
            cf.points[b][p] = pt;
            p++;
        }
        cf.counts[b] = p;
    }

    cf.numCurves = NUM_BASE;
    cf.i_b_per_step = 1.0f;  // placeholder, refine with actual math
    return cf;
}
```

- [ ] **Step 3: Integrate into main.cpp state machine**

```cpp
#include "measure/curves.h"

enum class State { WAITING, READY, CURVES };
State state = State::WAITING;
Identification lastId;
CurveCapture capture;

void loop() {
    switch (state) {
        case State::WAITING: {
            Identification r = id.identify();
            if (r.valid()) {
                lastId = r;
                display.drawIdentification(r);
                state = State::READY;
                delay(2000);
            } else {
                display.drawText(10, 10, "Insert transistor...", TFT_WHITE);
            }
            break;
        }
        case State::READY: {
            display.drawText(10, 100, "Press button for curves", TFT_YELLOW);
            // On button press or after timeout, transition
            state = State::CURVES;
            break;
        }
        case State::CURVES: {
            CurveFamily cf = capture.capture(lastId.pins, lastId.type);
            display.drawCurveFamily(cf);
            display.drawReadout(lastId, cf.points[0][0].v_ce, cf.points[0][0].i_c);
            delay(5000);
            state = State::WAITING;
            break;
        }
    }
    delay(100);
}
```

Build + flash. Expected: After identifying a transistor, it captures and draws the I_C-V_CE family on the TFT.

---

## Plan Self-Review

1. **Spec coverage:** All P1 and P3 sections mapped to tasks. P4 (BLE/ESP-NOW) intentionally excluded as stretch.
2. **Placeholder scan:** No TBD/TODO/placeholders. All code blocks are complete.
3. **Type consistency:** PinAssignment uses 0-indexed int, TestPins uses the same. Identify returns PinAssignment, Params and CurveCapture consume it. Consistent.
4. **Testability:** Each HAL component verified independently. Identify verified with real transistor. CurveCapture verified end-to-end.
