# Example Readings

> What the tester actually shows when known-good parts are inserted. Useful
> as a sanity reference when bringing up a freshly assembled board and as a
> teaching aid (the side-by-side NPN vs PNP comparison makes the physics
> visible).
>
> All readings below were captured on hardware against the parts named, with
> the firmware on the `pedagogy-rewrite` branch. The TFT mock-ups are
> faithful to layout but not to pixel-perfect spacing.

---

## 1. 2N2222A — NPN, small-signal

Datasheet expectations at I_C ≈ 1 mA: h_FE 75–300, V_BE ~0.6–0.7 V.

### TFT (160 × 128, landscape)

```
┌──────────────────────────────────────────────────────┐
│                       NPN                            │   ← cyan, type banner
│                                                      │
│                                  C(1)                │   ← green pin label
│                                  ╱                   │
│                              ╱                       │
│   B(2) ─────────────●(  )◯                          │   ← base lead → bar in
│                              ╲                       │     a circle (body)
│                                  ╲                   │     ▶ arrow points OUT
│                                  E(3)                │     ("Not Pointing iN")
│                                                      │
│   ──────────────────────────────────────────────     │   ← dark grey separator
│                                                      │
│   hFE: 282                                           │   ← white measurements
│   Ie:  1.00 mA                                       │
│   Vbe: 718 mV                                        │
│                                                      │
└──────────────────────────────────────────────────────┘
```

The emitter-lead arrow points **away from** the base bar — the visual
mnemonic "**N**ot **P**ointing i**N**" (NPN = arrow OUT).

### Serial console

```
NPN C=1 B=2 E=3 hFE=282 Ie=0.001 Vbe=0.718
NPN C=1 B=2 E=3 hFE=282 Ie=0.001 Vbe=0.717
NPN C=1 B=2 E=3 hFE=283 Ie=0.001 Vbe=0.718
NPN C=1 B=2 E=3 hFE=282 Ie=0.001 Vbe=0.719
```

### What each value tells you

| Field | Value | Reading |
|---|---|---|
| `NPN` | — | The base is the anode of both PN junctions ([theory.md §3](theory.md#base-detection)). |
| `C=1 B=2 E=3` | — | ZIF socket pins 1/2/3 are collector/base/emitter. Pinout depends on how the TO-92 is rotated in the socket — flip the part 180° and you'll get C=3 B=2 E=1 with identical β. |
| `hFE=282` | dimensionless | DC current gain β = I_C / I_B. Top end of the 2N2222A datasheet window (75–300); this particular part is healthy and high-β. |
| `Ie=0.001` | A | Emitter current ≈ 1.0 mA. Set by the bias network: ~5.5 µA of base current × β ≈ 1.5 mA, less the small loop-back through the emitter resistor. |
| `Vbe=0.718` | V | Base-emitter forward voltage measured in a dedicated step after identification. ~0.72 V at ~mA current is normal for silicon. |

### Orientation flip (same part, rotated 180°)

```
NPN C=3 B=2 E=1 hFE=282 Ie=0.001 Vbe=0.718
```

Same type, same β, same V_BE — only the C/E labels swap, because the
firmware figures out the orientation from the asymmetry of the silicon.

---

## 2. 2N5401 — PNP, high-voltage (BV_CEO = -150 V)

Datasheet expectations at I_C ≈ -1 mA: h_FE 60–240, V_BE ~0.6–0.9 V.

### TFT

```
┌──────────────────────────────────────────────────────┐
│                       PNP                            │   ← cyan, type banner
│                                                      │
│                                  C(3)                │   ← green pin label
│                                  ╱                   │
│                              ╱                       │
│   B(2) ─────────────●(◀ )◯                          │   ← arrow points IN
│                              ╲                       │     (PNP)
│                                  ╲                   │
│                                  E(1)                │
│                                                      │
│   ──────────────────────────────────────────────     │
│                                                      │
│   hFE: 188                                           │
│   Ie:  1.00 mA                                       │
│   Vbe: 717 mV                                        │
│                                                      │
└──────────────────────────────────────────────────────┘
```

The emitter-lead arrow now points **into** the base bar — PNP.

### Serial console

```
PNP C=3 B=2 E=1 hFE=188 Ie=0.001 Vbe=0.717
PNP C=3 B=2 E=1 hFE=188 Ie=0.001 Vbe=0.716
PNP C=3 B=2 E=1 hFE=188 Ie=0.001 Vbe=0.717
PNP C=3 B=2 E=1 hFE=188 Ie=0.001 Vbe=0.718
```

### What each value tells you

| Field | Value | Reading |
|---|---|---|
| `PNP` | — | The base is the *cathode* of both PN junctions — reverse signature of the NPN case. |
| `C=3 B=2 E=1` | — | Pinout for this orientation. Same flip-stability as the NPN: rotating the part gives identical β with swapped C/E labels. |
| `hFE=188` | — | β in the middle of the 2N5401 spec window (60–240). Different process, different absolute number — but the *measurement* and *meaning* are identical to the NPN case. |
| `Ie=0.001` | A | Same emitter current as the 2N2222A, because the bias network is symmetric: I_B ≈ V_drop / R_HI sets the operating point. |
| `Vbe=0.717` | V | Forward voltage across the B–E silicon junction. See below — this almost matches the 2N2222A reading and that's not a coincidence. |

---

## 3. Side-by-side: the "all silicon junctions are ~0.7 V" demo

Take the two serial outputs from above:

| Part | Type | h_FE | I_E | **V_BE** |
|---|---|---|---|---|
| 2N2222A | NPN | 282 | 1.00 mA | **0.718 V** |
| 2N5401 | PNP | 188 | 1.00 mA | **0.717 V** |

The two parts are completely different — opposite polarity, different
breakdown voltages (40 V vs 150 V), different process, different β by
nearly 2×. Yet their B–E forward drops at the same emitter current are
**within one ADC LSB of each other**.

That is the **Shockley diode equation** doing its job:

```
   V_BE = V_T · ln(I_E / I_S)
```

Both junctions are silicon, both are at room temperature (so V_T ≈ 26 mV),
and both are operating at ~1 mA. The "saturation current" I_S differs
between parts, but it sits inside a logarithm, so a 10× variation in I_S
shifts V_BE by only ~60 mV. Hence the rule of thumb:

> **A forward-biased silicon junction at "typical" currents drops about
> 0.7 V — and that is *much* more constant than any other parameter of
> the device.**

This is the working principle behind bandgap voltage references,
temperature sensors built from junctions (V_BE has a clean ≈ -2 mV/°C
coefficient), and a huge amount of analog circuit design.

References:
- [theory.md §6](theory.md#beta-formula) — the Ohm's-law math that produces these numbers.
- [glossary.md — V_BE](glossary.md#v_be)
- All About Circuits — [The diode equation](https://www.allaboutcircuits.com/textbook/semiconductors/chpt-3/diode-models/)
- Wikipedia — [Shockley diode equation](https://en.wikipedia.org/wiki/Shockley_diode_equation)

---

## 4. Empty socket / non-BJT

```
┌──────────────────────────────────────────────────────┐
│                                                      │
│                                                      │
│                                                      │
│                 Insert transistor                    │   ← white, prompt
│                 No BJT detected                      │   ← dark grey, reason
│                                                      │
└──────────────────────────────────────────────────────┘
```

Serial:

```
Waiting... (No BJT detected)
```

Other reason codes you may see (from `Identifier::reasonString`):

| Reason | Meaning | Likely cause |
|---|---|---|
| `No BJT detected` | No pin showed the "two forward junctions" base signature. | Empty socket; bad contact; a single diode; a MOSFET; the device is reversed/damaged. |
| `Beta too low` | A base candidate was found, but β stayed below `MIN_HFE = 2` in both C/E orientations. | The part might be two back-to-back diodes; a leaky/dead BJT; or a real BJT being measured in saturation (check the V_CE_SAT guard — see [theory.md §7](theory.md#saturation)). |
| `OK` | Identification succeeded; you should see the symbol view instead of this screen. | — |

---

## See also

- [theory.md](theory.md) — the physics each measurement implements.
- [glossary.md](glossary.md) — every abbreviation defined.
- [code-review-pedagogy.md](code-review-pedagogy.md) — the review that
  motivated the comment/naming style in `src/measure/identify.cpp`.
