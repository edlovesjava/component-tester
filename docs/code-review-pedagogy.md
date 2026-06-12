# Code Review — Pedagogical Clarity

> Goal of this review: make the firmware read like an explanation of *how a
> transistor works and how we measure it*, not just like working embedded code.
> Someone reading `identify.cpp` top-to-bottom should learn the physics on the way.

The code is correct and compact. Most changes below are renames, comments, and
small structural moves — no algorithmic rework — chosen so that each line tells
the reader *why* it is doing what it does in transistor terms.

---

## 1. Executive summary

| # | Theme | Where | Effort |
|---|-------|-------|--------|
| A | Name the physics: junctions, β, V_BE — not just `fwd`/`f++` | `src/measure/identify.cpp` | low |
| B | Lift magic numbers into named, sourced constants (V_CC, R_LO, R_HI, V_FWD bounds) | `identify.cpp`, `hal/pins.h` | low |
| C | Make the `TestPins` API speak the language it implements (Thévenin source: drive through 680 Ω vs 470 kΩ) | `hal/testpins.h/cpp`, `identify.cpp` | medium |
| D | Document the probe matrix indexing convention; it is currently asymmetric and surprising | `identify.cpp:19-69` | low |
| E | Split `measureHfe` into "bias the device" + "compute β from terminal voltages" | `identify.cpp:71-110` | medium |
| F | Lift the symbol drawing into a small data-driven helper; current pixel literals hide what the symbol means | `src/main.cpp:22-40` | low |
| G | Add a short `docs/theory.md` that the code can hyperlink to ("see §2 of theory.md") | new file | medium |
| H | Optional: a `models/Bjt` value-type so identification returns a self-describing object | new header | medium |

Items A–D are the ones that most directly turn the code into a teaching tool.
The rest amplify the effect.

---

## 2. The physics this code is doing — and where it is hidden

The identification algorithm is, in plain English:

1. A BJT contains **two PN junctions** that share the base:
   B–E and B–C. Both junctions, in isolation, behave roughly like diodes
   that drop ~0.3–0.9 V when forward-biased and look open otherwise.
2. The **base** is therefore the unique terminal that is connected to *both*
   of the other two terminals through a forward-biased diode.
3. Whether forward bias means "base is pulled higher than emitter/collector"
   (→ **NPN**) or "base is pulled lower" (→ **PNP**) tells you the polarity.
4. The remaining two pins are collector vs emitter. They are physically
   asymmetric (doping, geometry), so β is much larger in the correct
   orientation. **Try both orientations, keep the one with higher β.**
5. β itself is just `I_C / I_B`. With known resistors and the measured
   terminal voltages, both currents come out of Ohm's law.

All of this is true of the code today, but a reader has to reverse-engineer
each step from variable names like `fwd[i][j]`, `f++`, `h0`, `ie0`. The review
below proposes the smallest set of changes that make each of the five
statements above visible in the source.

---

## 3. File-by-file findings

### 3.1 `src/measure/identify.cpp`

This is the heart of the teaching story. Treat it as the file that has to be
the *most* readable.

#### A. Use physics vocabulary in names

| Current | Suggested | Why |
|---------|-----------|-----|
| `fwd[3][3]`, `rev[3][3]` | `forwardJunction[drive][sense]`, `reverseJunction[drive][sense]` | They are not generic flags; each `1` means "a forward-biased PN junction was detected between these two pins." |
| `probeFwd`, `probeRev` | `probeFromHigh`, `probeFromLow` *or* `probeNpnJunctions`, `probePnpJunctions` | The current names describe *direction of current* through the resistor network, not the physical question being asked. |
| `f++`, `r++` | `forwardJunctionsFromPin`, `reverseJunctionsToPin` | At line 132–139 we are literally counting "how many forward-biased diodes leave this pin." That sentence belongs in the variable. |
| `base = i; npn = true` | comment: *"NPN: base sits between two diodes whose anodes are the base, so it pulls both emitter and collector down → both forward junctions originate at the base when we drive it HIGH."* | The single most important comment in the file. |
| `h0`, `h1`, `ie0`, `ie1` | `betaIfPinAIsCollector`, `betaIfPinBIsCollector`, `ieIfPinAIsCollector`, `ieIfPinBIsCollector` | Names should encode the hypothesis being tested. |

#### B. Magic numbers → named, sourced constants

`identify.cpp:95-102` computes currents like this:

```cpp
i_b = (3.3f - vB) / 470000.0f;
i_c = (3.3f - vC) / 680.0f;
```

Three things are wrong with this from a teaching standpoint:

1. `3.3f` is the supply rail — the reader has to *know* that to interpret the
   subtraction. It also appears unlabelled in `measureHfe` and in the V_BE
   computation. Promote to `V_CC` in `hal/pins.h` (it already lives with the
   physical configuration there).
2. `470000.0f` and `680.0f` are the base- and collector-bias resistors. They
   should be `R_HI` / `R_LO` (matching the existing naming convention in
   `testpins.h`) and live next to them.
3. The expression `(V_CC - vB) / R_HI` is **Ohm's law applied to the base
   resistor**. That deserves a one-line comment. Likewise `vE / R_LO` is
   "voltage across the emitter resistor / R = I_E" — the most important
   measurement in the file.

Suggested form:

```cpp
// Base current: V_CC is pulled down to vB across the 470 kΩ bias resistor.
//   I_B = (V_CC - vB) / R_HI       (Ohm's law)
i_b = (V_CC - vB) / R_HI;

// Collector current: vC sits below V_CC by I_C·R_LO.
i_c = (V_CC - vC) / R_LO;

// Emitter current: vE rises above ground by I_E·R_LO.
if (i_e) *i_e = vE / R_LO;
```

The PNP branch is the mirror image (V_CC ↔ GND). A single comment block in
front of the if/else explaining "PNP just inverts the rails" pays for itself
five times over.

The hardcoded `0.2f` collector-emitter floor on `identify.cpp:90-91` is a
*saturation guard*: if V_CE is below ~0.2 V the transistor is in saturation
and β is not meaningful. Promote to `V_CE_SAT_MIN` with that one-sentence
comment. Same for `V_FWD_MIN` / `V_FWD_MAX` (lines 10–11): they encode "a
silicon PN junction conducts at ~0.6–0.7 V and we accept 0.3–0.9 V to
include Schottkys and Ge". Say that.

#### C. The probe matrix asymmetry is a bug-magnet

`probeFwd` writes `fwd[drive][j]` (line 32).
`probeRev` writes `rev[j][drive]` (line 60) — *transposed*.

Both indexings then get used in the same loop body on lines 132–139:

```cpp
if (fwd[i][j]) f++;
if (rev[j][i]) r++;
```

It works, but the reader has to notice and justify the swap. Two cleanups,
pick one:

1. **Pick one convention and stick to it.** Always store as
   `matrix[from][to]` where "from" is the side that is *higher* in voltage.
   Then `rev[j][drive]` becomes `rev[j][drive]` because in reverse probing the
   weakly-pulled-up pin (`j`) is the higher one — that's the same convention
   as forward, but it deserves a one-line comment at the top of `probeRev`.
2. **Wrap the matrix in a tiny struct** `JunctionMap` with a single accessor
   `bool diode(uint8_t anode, uint8_t cathode) const`. Then both probes feed
   the same logical map and the caller just asks the physics question:
   `if (jmap.diode(base, other)) ...`.

Either is fine; option 2 is the bigger pedagogical win.

#### D. Split `measureHfe` into "configure" + "compute"

The function currently does four things:

1. Decide which pin is driven how (lines 72–82).
2. Wait, measure three voltages (lines 83–88).
3. Check saturation (lines 90–91).
4. Compute currents and β (lines 93–110).

(1) is purely about applying a bias network around the BJT. (2)–(4) are
measurement and arithmetic. Splitting them lets a reader study the *circuit*
in isolation from the *math*:

```cpp
// Configure the bias network around the candidate transistor.
//   NPN: collector → V_CC via R_LO, emitter → GND via R_LO,
//        base      → V_CC via R_HI  (~7 µA base current).
//   PNP: rails swapped.
static void biasForBeta(uint8_t c, uint8_t b, uint8_t e, bool npn);

// Given a biased BJT, read terminal voltages and return β = I_C / I_B.
// Returns 0 if the device is in saturation (V_CE < V_CE_SAT_MIN).
static float computeBeta(uint8_t c, uint8_t b, uint8_t e, bool npn, float* i_e);
```

This decomposition matches how the topic is taught: *first* set up the bias
point, *then* compute small-signal parameters.

#### E. Name the steps in `identify()`

`identify()` reads as a sequence of unlabelled loops. Insert four short
section comments that map directly onto §2 of this review:

```cpp
Identification Identifier::identify() {
    // Step 1: probe every (drive, sense) pair to find PN junctions.
    // Step 2: a pin that is anode of two forward junctions is the BJT base.
    // Step 3: orientation of the junctions tells us NPN vs PNP.
    // Step 4: try both (C, E) assignments; keep the one with higher β.
    // Step 5: with the device biased, V_BE is the base-emitter drop.
```

Each step then has a 5–15 line block under it. The reader can stop after the
comments and still know what the algorithm does.

---

### 3.2 `src/hal/testpins.h` / `testpins.cpp`

The class is a clean abstraction over "three test nodes, each with a
low-impedance and a high-impedance driver." The abstraction *itself*,
however, doesn't tell the reader why two drivers exist. Two improvements:

1. **Rename the parameters** of `setPin(uint8_t idx, PinDrive lo, PinDrive hi)`
   to `setPin(uint8_t pin, PinDrive loZ, PinDrive hiZ)` — and document at the
   declaration that `loZ` is the 680 Ω (Thévenin ≈ 680 Ω) path and `hiZ` is
   the 470 kΩ path. Right now the names `lo` / `hi` *look* like "drive low"
   and "drive high," which is wrong.
2. **Add an enum convenience layer** that callers in `identify.cpp` actually
   want:

   ```cpp
   enum class NodeRole {
       OPEN,             // both Hi-Z
       STRONG_HIGH,      // 680Ω to V_CC  (low-Z source)
       STRONG_LOW,       // 680Ω to GND   (low-Z sink)
       WEAK_HIGH,        // 470kΩ to V_CC (current-limited base bias)
       WEAK_LOW,         // 470kΩ to GND
   };
   void setNode(uint8_t pin, NodeRole role);
   ```

   With this, `identify.cpp:73-82` reads as physics, not GPIO bookkeeping:

   ```cpp
   tp.setNode(c, NodeRole::STRONG_HIGH);   // V_CC through 680Ω → collector
   tp.setNode(e, NodeRole::STRONG_LOW);    // GND through 680Ω → emitter
   tp.setNode(b, NodeRole::WEAK_HIGH);     // V_CC through 470kΩ → ~7µA base bias
   ```

   The current `setPin(idx, DRV_HIGH, DRV_HIZ)` form requires the reader to
   keep three pieces of state in their head.

3. Minor: the explicit out-of-class redefinitions of `PIN_LO` / `PIN_HI` on
   `testpins.cpp:4-5` are a C++17 leftover (inline `constexpr` member arrays
   are definitions). They're harmless but distracting in a teaching file —
   either drop them or add a single comment noting "C++14 ODR-definition,
   needed before inline static."

---

### 3.3 `src/main.cpp`

`drawSymbol` (lines 22–40) currently uses raw pixel literals. For a teaching
tool, the *layout* should be derivable from the symbol's anatomy:

```cpp
// BJT symbol anatomy on the 160×128 display:
//   - vertical bar  = base region of the silicon
//   - left line     = base lead
//   - upper diagonal = collector lead
//   - lower diagonal = emitter lead (arrow head encodes NPN vs PNP)
struct SymbolGeom {
    int barX, barY, barW, barH;
    int junctionX, junctionY;
    int collectorTipX, collectorTipY;
    int emitterTipX,   emitterTipY;
    int baseTipX,      baseTipY;
};
static constexpr SymbolGeom SYMBOL = {77, 30, 3, 16, /*...*/};
```

The arrow direction is the only thing that encodes NPN vs PNP visually — and
this is the standard "Not Pointing iN" mnemonic. Add a one-line comment:

```cpp
// Arrow on the emitter lead: points outward for NPN, inward for PNP
// ("Not Pointing iN" mnemonic).
```

`drawMeasurements` is fine. Consider showing units in a fixed column so they
don't jump as the value width changes (right-justify the number, fixed
unit position) — a small UX/teaching nicety.

`loop()` reads cleanly. One pedagogical addition: when no transistor is
detected, print *why* on the second line of the display ("no junctions
found" vs "no base candidate" vs "β below MIN_HFE"). The plumbing for this
is already there — `identify()` discards the information at lines 145 and
170. Returning a small enum reason alongside `Identification` would let the
user see the algorithm working in real time, which is excellent for
teaching.

---

### 3.4 `src/hal/adc.cpp`

Clean and correct. The only teaching-friendly addition: a one-line comment on
`voltageForRaw` explaining the ±4.096 V FSR and 16-bit signed range:

```cpp
// ADS1115 in GAIN_ONE: ±4.096 V full-scale, 16-bit signed.
// One LSB ≈ 125 µV — fine for mV-level V_BE measurements.
return raw * 4.096f / 32768.0f;
```

The spec already says this in §3.3; quoting it at the implementation site
closes the loop.

---

### 3.5 `src/hal/stimulus.cpp`

Not used in P1 identification — fine. For teaching: add a header comment in
`stimulus.h` saying "Phase-2+ feature: RC-filtered PWM as a coarse DAC for
base/collector bias sweeps. Not exercised by `Identifier::identify()`." That
prevents a reader from going on a wild goose chase looking for the call site.

---

## 4. Suggested new file: `docs/theory.md`

The code keeps wanting to refer to physics. Rather than inline three
paragraphs of theory into `identify.cpp`, put them in a single
`docs/theory.md` with anchored sections:

- `#diode-junction` — why we look for 0.3–0.9 V drops
- `#base-detection` — the "anode of two junctions" argument
- `#bias-network` — why 470 kΩ for base, 680 Ω for collector
- `#beta-formula` — β = I_C / I_B from terminal voltages
- `#saturation` — what V_CE_SAT means and why we reject those measurements

Then code comments become one-line references:

```cpp
// See docs/theory.md#base-detection
if (forwardJunctionsFromPin == 2) { base = i; npn = true; break; }
```

This keeps the source dense without losing the teaching narrative.

---

## 5. Optional: a self-describing result type

`Identification` (`identify.h:14-23`) is currently a passive struct. It would
read better as a value type that knows how to describe itself:

```cpp
struct Identification {
    TransistorType type = TransistorType::UNKNOWN;
    PinAssignment  pins = {0, 0, 0};
    float v_be = 0.0f;
    float hfe  = 0.0f;
    float i_e  = 0.0f;
    Reason reason = Reason::OK;       // why valid()==false, when it is

    bool valid() const;
    void printDescription(Print& out) const;   // used by main.cpp + Serial
};
```

`printDescription` becomes the single source of truth for the human-readable
form, and `main.cpp` shrinks. For a teaching artifact this is high leverage:
the *reader* gets one place that shows them how to interpret a result.

---

## 6. What I would do first

If you want a single, time-boxed pass that yields the biggest pedagogical
win, do these in order — they are all in `src/measure/identify.cpp`:

1. Add the five `// Step N:` comments inside `identify()` (10 min).
2. Lift `V_CC`, `R_LO`, `R_HI`, `V_CE_SAT_MIN` into named constants and reuse
   them in `measureHfe` and the V_BE block (15 min).
3. Rename `fwd` / `rev` / `f` / `r` to physics-flavored names; add the
   "anode of two diodes = base" comment beside the detection loop (15 min).
4. Add the Ohm's-law comments next to the three current calculations
   (5 min).

That alone — perhaps an hour of work, zero behavior change — turns
`identify.cpp` into something you can hand someone learning what a BJT is.

The bigger restructurings (§3.1.D, §3.2.2, §3.5.G) are worth doing once the
P2 curve-tracer code lands, because they pay off across that larger file
set too.
