# MOSFET Testing Design

## Overview

Extend the component tester to identify and characterize N-channel and P-channel
MOSFETs using the existing hardware. Two display modes, switched via the BOOT
button (GPIO0):

- **Mode 1** — Identify type (N-CH / P-CH), pinout (G/D/S), and key parameters
- **Mode 2** — Swept characterization curves (I_D vs V_DS at multiple V_GS)

BJT characterization curves (I_C vs V_CE at multiple I_B) are added as Mode 2
for BJTs as well.

## Hardware

All measurements use the existing test-pin GPIOs with PWM capability:

| Role | GPIO path | Resistor |
|------|-----------|----------|
| Gate voltage | HI GPIO → ZIF | 470 kΩ |
| Drain supply | LO GPIO → ZIF | 680 Ω |
| Source return | LO GPIO → ZIF | 680 Ω |
| Voltage sense | ZIF → ADS1115 (chan 0–2) | — |

No additional hardware required. ESP32 supports PWM on any GPIO, so the existing
HI/LO pins are reconfigured for PWM when sweeping.

## Identification Algorithm

### Body Diode Detection

Run the same forward/reverse probe matrices used for BJT identification:

- `fwd[drive][other]` — driving `drive` HIGH through 680 Ω, `other` shows
  0.3–0.9 V drop → 1
- `rev[other][drive]` — driving `drive` LOW through 680 Ω with `other` pulled
  HIGH through 470 kΩ, `other` shows 0.3–0.9 V rise → 1

For BJT: one pin (base) has `f=2` or `r=2` in the existing detection.

For MOSFET: no pin has `f=2` or `r=2`. Instead:

1. **Find the gate** — the pin where ALL matrix entries involving it are 0
   (gate is an open circuit with no DC path to D or S).
2. **Find the body diode** — the remaining two pins show a diode in exactly
   one direction:
   - `fwd[A][B] = 1` and `rev[A][B] = 1` → diode A→B (A = anode, B = cathode)
   - N-channel: body diode S→D, so A = S, B = D
   - P-channel: body diode D→S, so A = D, B = S

### Type Confirmation

The body diode direction alone is ambiguous — both N and P channel have a
single diode between two pins. Apply gate bias to confirm:

1. **Try N-channel**: drive G = 3.3 V (470 kΩ), S(anode) = GND (680 Ω),
   D(cathode) = VCC (680 Ω). If I_D flows → N-channel confirmed.
   Assignments: S = anode, D = cathode.
2. **Try P-channel**: drive G = GND (470 kΩ), S(cathode) = VCC (680 Ω),
   D(anode) = GND (680 Ω). If I_D flows → P-channel confirmed.
   Assignments: D = anode, S = cathode.

The `Identification` result uses enum values `N_CHANNEL` and `P_CHANNEL`.

## Mode 1: Identify + Parameters

### Display Layout

```
N-CH                              ← type, cyan, cented at y=2
  (MOSFET symbol)
────────────────────              ← separator at y=74
Vgs(th): 1.85 V                   ← white at y=80
Rds(on): 5.3 Ω                    ← white at y=92
Id:      2.15 mA                  ← white at y=104
```

### MOSFET Symbol

Simplified symbol using the same coordinate space as BJT:

```
    D(n) at (94, 22)
     │
 G(n)┤      ← gate at (58, 38) → (78, 38)
     │      ← vertical bar (78, 26) → (78, 50)
     │
     ╲
      ╲
     S(n) at (94, 52)
```

Small filled-triangle arrow on the source diagonal:
- N-channel: points upward (toward drain)
- P-channel: points downward (away from drain)

### V_GS(th) Measurement

- Config: S = GND (680 Ω), D = VCC (680 Ω), G = PWM through 470 kΩ
- Sweep G PWM from 0 → 100 % in ~50 steps
- At each step: read V_G, V_D, V_S via ADC; compute I_D = (3.3 V − V_D) / 680 Ω
- V_GS(th) = V_GS at I_D = 250 µA (standard definition), found by linear
  interpolation between steps
- If V_GS > 3.3 V without reaching 250 µA → display ">3.3 V"

### R_DS(on) and I_D

- At max V_GS (PWM = 100 %), measure V_DS and I_D
- R_DS(on) = V_DS / I_D
- I_D displayed in mA

## Mode 2: Characterization Curves

### MOSFET: I_D vs V_DS at Multiple V_GS

For each V_GS step (3–4 values spanning below to above V_GS(th)):
- Sweep V_DS by varying D GPIO PWM from 0 → 100 %
- At each of ~30 points: read V_D, V_S, compute I_D and V_DS
- Record (V_DS, I_D) pairs

### BJT: I_C vs V_CE at Multiple I_B

For each I_B step (3–4 values):
- Sweep V_CE by varying C GPIO PWM from 0 → 100 %
- At each of ~30 points: read V_C, V_E, compute I_C and V_CE
- Record (V_CE, I_C) pairs

### Plot Rendering

- Plot area: ~100 × 80 px on the 128 × 160 landscape display
- X axis: V_DS (or V_CE), 0–3.3 V range
- Y axis: I_D (or I_C), auto-scaled to max measured value
- Axis labels and tick marks
- 3–4 lines drawn with different brightness/color or dashing
- Beside the plot: small text listing the V_GS (or I_B) for each curve

## Mode Switching

- **BOOT button (GPIO0)**: short press cycles modes
- On reset: default to Mode 1
- If no component detected: show "Insert component" regardless of mode
- Mode 2 entry triggers a fresh sweep (~1–2 s)

## Code Organization

### New `TransistorType` values

```cpp
enum class TransistorType : uint8_t {
    UNKNOWN, NPN, PNP, N_CHANNEL, P_CHANNEL
};
```

### New files

| File | Purpose |
|------|---------|
| `src/measure/mosfet.cpp` | MOSFET identification logic |
| `src/measure/sweep.cpp` | Shared sweep/characterization logic |
| `src/display/display.h` | Display drawing helpers |

### Existing files

| File | Changes |
|------|---------|
| `src/measure/identify.h` | Add `N_CHANNEL`, `P_CHANNEL` to enum; extend `Identification` if needed |
| `src/main.cpp` | Add BOOT button detection, mode state, Mode 2 rendering |
| `src/hal/pins.h` | Add `PIN_BOOT` for GPIO0 |

### Sweep control flow

```cpp
struct SweepPoint { float v_ds, i_d; };

// For MOSFET:
SweepResult sweepMosfet(
    uint8_t gate_pin, uint8_t drain_pin, uint8_t source_pin,
    bool npn,               // true = N-channel
    float v_gs,             // fixed V_GS for this curve
    uint16_t steps = 30
);

// For BJT:
SweepResult sweepBjt(
    uint8_t base_pin, uint8_t collector_pin, uint8_t emitter_pin,
    bool npn,
    float i_b,              // fixed I_B for this curve (via PWM on base)
    uint16_t steps = 30
);
```

## Error Handling

| Condition | Behavior |
|-----------|----------|
| No component | Display "Insert component" |
| BJT found | Mode 1: BJT params; Mode 2: I_C vs V_CE curves |
| MOSFET found | Mode 1: MOSFET params; Mode 2: I_D vs V_DS curves |
| Ambiguous pinout | Return UNKNOWN |
| V_GS(th) > 3.3 V | Display ">3.3 V" |
| ADS1115 failure | Display "ADC FAIL" (existing) |
