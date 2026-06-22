# Diode Testing Design

## Overview

Extend the component tester to identify and characterize **two-terminal diodes**
using the existing hardware — no PCB changes, no boost stage, no extra parts.

Covered devices:
- **Silicon signal/rectifier diodes** (1N4148, 1N400x, etc.) — V_F ~0.55–0.75 V
- **Schottky / Germanium diodes** (1N5817, 1N34, BAT54) — V_F ~0.2–0.4 V
- **LEDs**, with approximate colour-band inference from V_F:
  - Red/yellow/orange: V_F ~1.6–2.1 V
  - Green: V_F ~2.0–2.5 V
  - Blue/white: V_F ~2.7–3.2 V (upper edge unreliable — see Caveats)

**Explicit non-goals (v1):**
- **No Zener detection.** Reverse breakdown above 3.3 V cannot be reached with
  the existing supply. Treated as a follow-up that requires the
  [boost stage spec](2026-06-12-boost-stage-design.md) (planned).
- **No high-current V_F.** Test current is set by the 680 Ω bias resistor and
  the 3.3 V rail, so I_F sits in the 0.1–5 mA range depending on V_F. This
  is below the 10–100 mA at which datasheets typically quote V_F, so the
  reported V_F will be a few hundred mV *lower* than the datasheet value.
- **No leakage / I_R characterization.** The 470 kΩ pull-up has too much
  sensitivity-to-leakage to give a useful reverse-current number.
- **No dynamic parameters** (junction capacitance, t_rr). Out of scope; would
  require a step-recovery + scope-grade front end.

Mode 1 of the unified tester adds a third device class:

| Class | Existing | Status |
|-------|----------|--------|
| BJT (NPN / PNP) | Yes | Shipping |
| MOSFET (N-CH / P-CH) | Spec'd | [Planned](2026-06-11-mosfet-testing-design.md) |
| **Diode (Si / Schottky / LED)** | — | **This spec** |

Identification falls through in that order: BJT → MOSFET → Diode → Unknown.
A diode is what we report when no three-terminal device matches.

## Hardware

All measurements use the existing test-pin GPIOs. No PCB changes.

| Role | GPIO path | Resistor |
|------|-----------|----------|
| Anode drive | LO GPIO → ZIF | 680 Ω |
| Cathode return | LO GPIO → ZIF | 680 Ω |
| Voltage sense | ZIF → ADS1115 (chan 0–2) | — |

The third (unused) ZIF pin is held at Hi-Z during diode identification to
avoid loading the device.

## Identification Algorithm

### Probe reuse

Run the same `fwd[drive][other]` / `rev[other][drive]` matrices already used
for BJT identification (`probeFwd` / `probeRev` in `identify.cpp`). Both
matrices are 1 wherever a forward-biased silicon-junction-shaped drop is
observed (0.3–0.9 V on the existing thresholds).

### Detection order

Detection is a fall-through. The first signature that matches wins:

1. **BJT** — exactly one pin has `f == 2` (NPN base) or `r == 2` (PNP base).
2. **MOSFET** — gate pin shows zero entries in both matrices; the other two
   show a body-diode signature in exactly one direction. (See the
   [MOSFET spec](2026-06-11-mosfet-testing-design.md).)
3. **Diode** — exactly one `fwd[A][B] == 1` and one `rev[B][A] == 1`, with
   all other entries 0. Pin `C` (the third) is unused.
4. **Unknown** — none of the above.

### Diode signature

For a diode with anode = A, cathode = B, unused pin = C, the matrices look
like this (rows = drive pin, columns = sense pin):

```
              fwd[drive][sense]              rev[drive][sense]
              A    B    C                    A    B    C
       A      -    1    0                    -    0    0
       B      0    -    0                    1    -    0
       C      0    0    -                    0    0    -
```

Verbal restatement:
- Drive A HIGH, B LOW: forward bias the diode → forward drop ~0.6 V observed
  → `fwd[A][B] = 1`.
- Drive A LOW, B pulled HIGH: diode reverse-biased → no current → B sits at
  V_CC → drop is ~3.3 V (out of accept window) → `rev[A][B] = 0`.
- Drive B LOW, A pulled HIGH: diode forward-biased through 470 kΩ → A sits
  ~0.4–0.6 V above B → `rev[B][A] = 1`.
- Drive B HIGH, A LOW: reverse-biased → `fwd[B][A] = 0`.
- Drive C in either direction → C is electrically isolated from A and B → all
  entries involving C are 0.

The detector accepts the diode if and only if this exact pattern is found.
Tolerance: we additionally accept `rev[B][A]` reading slightly outside the
0.3–0.9 V window for Schottkys (their V_F under 470 kΩ pull-up can drop to
~0.15 V at the few-µA current that bias delivers). The forward-probe
`fwd[A][B] = 1` is the load-bearing test.

### Pseudocode

```cpp
// After BJT and MOSFET detection have failed:
int anodes = 0, cathodes = 0;
uint8_t A = 0xFF, B = 0xFF;
for (uint8_t i = 0; i < 3; i++) {
    for (uint8_t j = 0; j < 3; j++) {
        if (i == j) continue;
        if (fwd[i][j]) { anodes++;   A = i; B = j; }
        // We do NOT require the rev[][] entry to match here — Schottkys can
        // miss the threshold on the 470 kΩ pull-up.
    }
}
if (anodes != 1) return Reason::AMBIGUOUS;
// Verify the third pin is unused: scan the row + column of C, all entries
// must be 0 in both fwd and rev.
uint8_t C = 3 - A - B;
if (anyEntryInvolvingPin(fwd, C) || anyEntryInvolvingPin(rev, C))
    return Reason::AMBIGUOUS;
// Done — A is anode, B is cathode.
```

## V_F Measurement

Once the diode is identified, measure V_F under the **stronger** of the two
bias paths (680 Ω drive both sides — same drive used in BJT V_BE measurement):

- Drive A HIGH through 680 Ω (anode to V_CC)
- Drive B LOW through 680 Ω (cathode to GND)
- Drive C Hi-Z
- Settle ~10 ms
- Read V(A), V(B) via ADS1115
- **V_F = V(A) − V(B)**
- **I_F = (3.3 V − V(A)) / 680 Ω** (current through the anode-side resistor;
  equal to current through the cathode-side resistor minus a small offset
  set by V(B), which is small).

Expected ranges, given V_CC = 3.3 V and R_LO = 680 Ω:

| Device | V_F | I_F | Notes |
|--------|-----|-----|-------|
| Schottky (1N5817) | 0.20–0.30 V | ~4.5 mA | well above the ADC noise floor |
| Si signal (1N4148) | 0.55–0.65 V | ~3.9 mA | datasheet typ. is 0.7 V @ 10 mA |
| Si rectifier (1N4001) | 0.55–0.70 V | ~3.8 mA | same |
| Red LED | 1.7–2.0 V | ~2.0 mA | visibly glowing |
| Yellow/green LED | 2.0–2.4 V | ~1.5 mA | visibly glowing |
| Blue/white LED | 2.8–3.2 V | ~0.2 mA | dim or near-cutoff (see Caveats) |

## Type Classification

After V_F is read, classify by table lookup. Boundaries chosen so a
real-world part's spread does not straddle two categories.

```cpp
enum class DiodeKind : uint8_t {
    SCHOTTKY,        // V_F < 0.40 V
    SILICON,         // 0.40 V ≤ V_F < 1.40 V
    LED_RED,         // 1.40 V ≤ V_F < 2.10 V
    LED_GREEN,       // 2.10 V ≤ V_F < 2.65 V
    LED_BLUE_WHITE,  // V_F ≥ 2.65 V
    UNKNOWN,
};
```

LED colour inference is **approximate**: a "red" LED with high efficiency can
test as low as 1.6 V, and a "green" LED of the old GaP type tests around
2.2 V like a yellow. Treat the colour label as a hint, not a guarantee.

## Display Layout

160 × 128 landscape ST7735, same screen layout as the BJT view.

```
              DIODE                      ← cyan banner at y=2,
                                            colored by class:
                                            SCHOTTKY / DIODE / LED
                                                
                                                
   A(1) ──▶|── K(2)                      ← horizontal diode symbol
                                            at vertical centre
            (3) N/C                      ← small grey label, unused pin
                                                
   ──────────────────────                ← separator at y=74
                                                
   Vf:   718 mV                          ← measurements
   If:    3.81 mA
   Type: Silicon                         ← class label (cyan)
```

### Diode symbol coordinates

Pick coordinates that re-use the same x-range as the BJT body so the layout
"feels" the same.

```
Anode lead:       horizontal line from (32, 38) to (74, 38)
Triangle:         filled triangle, tip at (90, 38),
                  base from (74, 30) to (74, 46)
Cathode bar:      vertical line from (90, 30) to (90, 46)
Cathode lead:     horizontal line from (90, 38) to (132, 38)

Pin labels:
  Anode (A):     "A(n)" at (8, 34)
  Cathode (K):   "K(n)" at (134, 34)
  Unused (N/C):  "(n) N/C" at (60, 56), in TFT_DARKGREY
```

(Following the existing convention, ZIF pin numbers are 1–3, internal pin
indices 0–2.)

### Colour scheme

- DIODE banner: cyan (same as NPN/PNP)
- SCHOTTKY: magenta — distinguishes from silicon at a glance
- LED: yellow — LEDs are special, deserve highlight
- Symbol: white
- Pin labels: green (same as BJT view)
- N/C label: dark grey
- Measurement values: white
- Type label line: cyan

## Output Format

Serial:

```
DIODE A=1 K=2 Vf=0.618 If=0.00388 type=Si
SCHOTTKY A=1 K=2 Vf=0.298 If=0.00442 type=Schottky
LED A=1 K=2 Vf=1.92 If=0.00203 type=red
NONE                                          (empty socket)
```

`Identifier::identify()` returns:

```cpp
struct Identification {
    DeviceClass cls;           // BJT, MOSFET, DIODE, UNKNOWN
    union {
        BjtInfo bjt;
        MosfetInfo mos;
        DiodeInfo diode;
    };
};

struct DiodeInfo {
    PinPair pins;              // anode, cathode (indices 0..2)
    uint8_t unused_pin;        // 0..2
    float v_f;                 // forward voltage (V)
    float i_f;                 // forward current at the test bias (A)
    DiodeKind kind;
};
```

Existing `BjtInfo` and the planned `MosfetInfo` become siblings under the
`Identification` union — the BJT code path remains unchanged externally.

## Test Plan

Validation against a known parts bin:

- [ ] 1N4148 (Si signal): expect V_F ≈ 0.60 V, type=Silicon, A/K assignment
      stable across orientation flip.
- [ ] 1N4001 (Si rectifier): expect V_F ≈ 0.65 V, type=Silicon.
- [ ] 1N5817 (Schottky): expect V_F ≈ 0.25 V, type=Schottky.
- [ ] 5 mm red LED: expect V_F ≈ 1.85 V, type=LED_RED, faint visible glow.
- [ ] 5 mm green LED: expect V_F ≈ 2.10 V, type=LED_GREEN.
- [ ] 5 mm blue LED: expect V_F ≈ 2.90 V or "near cutoff" warning,
      type=LED_BLUE_WHITE.
- [ ] Two 1N4148s back-to-back (anode-anode or cathode-cathode): expect
      Reason::AMBIGUOUS (this rules out the "diode pair" false-positive).
- [ ] Empty socket: expect NONE.
- [ ] BJT inserted while in unified-identify mode: expect BJT path to win
      (regression guard).

## Caveats

- **Current is below datasheet conditions.** A 1N4148 at 4 mA reports
  V_F ≈ 0.60 V; the datasheet's 1.0 V at 10 mA is not what we will see.
  This is the device behaving according to the Shockley diode equation, not
  a bug. Document this on the display somehow (footnote or low-contrast tag
  "@ ~4 mA") to avoid confusion.
- **Blue/white LEDs at the upper V_F edge.** A 3.2 V V_F LED on a 3.3 V rail
  draws only ~150 µA through 680 Ω — small but measurable. A 3.3 V V_F LED
  is indistinguishable from "open." When measured I_F drops below ~50 µA
  while V_F is above ~3.0 V, display a "near cutoff" warning rather than
  pretending the reading is accurate.
- **Schottky pull-up edge case.** Under the 470 kΩ reverse-probe pull-up, a
  Schottky's V_F can drop as low as 0.15 V — below the 0.3 V `V_FWD_MIN`
  threshold. This is why the algorithm uses the forward-probe entry as the
  load-bearing detection signal and treats the reverse-probe entry as
  confirmatory rather than required.
- **Two-diode false-positive for BJT.** The BJT algorithm already protects
  against this via the β check (a back-to-back diode pair gives β ≈ 0). The
  diode path only runs after BJT and MOSFET have rejected the device, so
  this is a non-issue.

## Future Extensions

These belong in follow-up specs once this one is shipping:

1. **Zener detection** — requires the
   [boost stage spec](2026-06-12-boost-stage-design.md) to apply reverse
   voltages above 3.3 V. Will add a sweep, look for the
   reverse-breakdown plateau, and report V_Z.
2. **Higher-current V_F characterization** — same boost stage repurposed as
   a current source on the anode side, sweep up to ~30 mA, plot V_F vs I_F.
3. **Wavelength inference for LEDs** — once V_F is measured at multiple
   currents, the ideality factor and band-gap fit could give a much sharper
   colour estimate than the current single-point lookup.
4. **Junction temperature compensation** — V_F has a ~−2 mV/°C coefficient;
   for the bench instrument this is a few-percent error and not worth fixing,
   but a calibration mode that asks for ambient temperature would tighten the
   colour classification thresholds.

## References

- [docs/theory.md §2 — The diode test](../../theory.md#diode-junction)
- [docs/glossary.md — Diode](../../glossary.md#diode)
- [docs/glossary.md — Schottky diode](../../glossary.md#schottky-diode)
- [Shockley diode equation (Wikipedia)](https://en.wikipedia.org/wiki/Shockley_diode_equation)
- [LED forward-voltage by colour (Wikipedia)](https://en.wikipedia.org/wiki/Light-emitting_diode#Colors_and_materials)
