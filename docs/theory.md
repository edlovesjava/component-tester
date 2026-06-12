# Theory of Operation

> How the Component Tester decides what kind of transistor is in its socket
> and how it measures the device's parameters — written so the firmware
> (`src/measure/identify.cpp`) reads like an implementation of this document.
>
> Assumes you have seen Ohm's law, know what a diode is, and have at least
> heard the words "NPN" and "transistor." Any term that abbreviates something
> is defined in [glossary.md](glossary.md); every term-of-art is hyperlinked
> on first use.

---

## 1. What is a BJT, really? <a id="bjt-overview"></a>

A **BJT** (bipolar junction transistor) is a sandwich of three doped
semiconductor regions: **N–P–N** or **P–N–P**. The middle region is called
the *base*; the outer regions are the *collector* and the *emitter*.

```
          NPN                              PNP
        ┌─────┐                          ┌─────┐
   C ── │  N  │                       C ─│  P  │
        ├─────┤                          ├─────┤
   B ── │  P  │ ← thin base region    B ─│  N  │ ← thin base region
        ├─────┤                          ├─────┤
   E ── │  N  │                       E ─│  P  │
        └─────┘                          └─────┘
```

Between the base and each outer region is a **PN junction** — the same
structure as a [diode](glossary.md#diode). So a BJT can be thought of, for
the limited purpose of *finding which lead is which*, as **two diodes that
share an anode (for NPN) or a cathode (for PNP)**:

```
   NPN (shared anode = base)        PNP (shared cathode = base)
                                                                
        C                                  C                    
        △ (cathode)                        ▽ (anode)            
        │                                  │                    
   B ──●── E                            B ●── E                 
        │                                  │                    
        △ (cathode)                        ▽ (anode)            
        E                                  E                    
                                                                
   Both diodes' anodes meet at B.      Both diodes' cathodes    
                                       meet at B.                
```

This "two-diode model" is wrong for explaining *gain* (a real BJT amplifies;
two back-to-back diodes do not), but it is *exactly right* for our
identification step. We exploit it directly.

External background:
- All About Circuits — [Bipolar Junction Transistors (intro chapter)](https://www.allaboutcircuits.com/textbook/semiconductors/chpt-4/introduction-bjt/)
- HyperPhysics — [The Bipolar Transistor](http://hyperphysics.phy-astr.gsu.edu/hbase/Solids/trans.html)
- Wikipedia — [Bipolar junction transistor](https://en.wikipedia.org/wiki/Bipolar_junction_transistor)

---

## 2. The diode test <a id="diode-junction"></a>

A silicon **PN junction** conducts when forward-biased (anode pulled higher
than cathode) and the voltage across it sits at roughly **0.6–0.7 V**
regardless of the current (within a couple of decades of current). When
reverse-biased, no significant current flows.

So if we drive one of our three test nodes through a resistor and pull
another low through another resistor, then look at the voltage across the
pair, we get one of two outcomes:

| Observation | Interpretation |
|---|---|
| ΔV ≈ 0.6 V (we accept 0.3 – 0.9 V) | a forward-biased PN junction is between them |
| ΔV ≈ 0 V or the drive rail | no junction in that direction |

The bounds `V_FWD_MIN = 0.3 V` and `V_FWD_MAX = 0.9 V`
(`identify.cpp`) widen the accept window so we also recognise Schottky and
germanium parts on the low end and stacked-junction parts on the high end,
without false-triggering on resistor-divider voltages near V_CC/2.

References:
- SparkFun — [Diodes](https://learn.sparkfun.com/tutorials/diodes/all)
- Electronics Tutorials — [Junction Diode](https://www.electronics-tutorials.ws/diode/diode_3.html)
- Wikipedia — [P–n junction](https://en.wikipedia.org/wiki/P%E2%80%93n_junction)

---

## 3. Finding the base <a id="base-detection"></a>

From §1, a BJT looks like two diodes sharing a terminal.

- **NPN**: both diodes have their anode at the base. If we drive the base
  HIGH and pull both others LOW, **two forward junctions appear, both
  starting from the base.**
- **PNP**: both diodes have their cathode at the base. If we drive the base
  LOW and weakly pull the others HIGH, **two forward junctions appear, both
  pointing into the base.**

The firmware implements this directly:

```
For each pin i in {0,1,2}:
    drive i HIGH (others LOW)  → record forwardJunction[i][j]
    drive i LOW  (others HIGH) → record reverseJunction[i][j]

For each pin i:
    if forwardJunction[i][·] is 1 for both other pins → i is the NPN base
    if reverseJunction[i][·] is 1 for both other pins → i is the PNP base
```

If neither test fires on any pin, we report `Reason::NO_BASE_CANDIDATE` —
the device in the socket is not a recognisable BJT (no transistor, a single
diode, a MOSFET, a dead part, or something miswired).

Why this is sufficient: the collector and emitter, taken in isolation, are
not connected to each other by a forward-biased junction (the C–B and B–E
junctions are *back-to-back* between them, so at most one can be forward at
a time). So only the base can produce the "two forward junctions"
signature.

---

## 4. Distinguishing collector from emitter <a id="collector-vs-emitter"></a>

The two outer regions of the silicon are *not* interchangeable, even though
the simplified two-diode model treats them as if they were. A real BJT is:

- **Asymmetric in doping**: emitter is heavily doped, collector lightly.
- **Asymmetric in geometry**: collector region is physically larger so it
  can collect carriers swept across the thin base.

As a result, **β (forward current gain) is much higher in the correct
orientation** than in the reversed one — often by 10× to 100×. So the
algorithm is brutally simple: **try both candidate assignments for (C, E)
and keep the one with the higher β**.

If both candidates yield β below `MIN_HFE = 2`, we return
`Reason::BETA_TOO_LOW` and refuse to report a result.

References:
- Sedra & Smith (textbook) — *Microelectronic Circuits*, ch. 6, "BJT operation in the active mode"
- Khan Academy — [BJT current relationships](https://www.khanacademy.org/science/electrical-engineering/ee-semiconductor-devices/ee-bipolar-junction-transistors/v/ee-bjt-current-relationships)

---

## 5. The bias network <a id="bias-network"></a>

To measure β we need to put the BJT into its **active region** — the regime
where small changes in base current produce large, proportional changes in
collector current. We do that with a fixed-resistor bias network:

```
           NPN bias                            PNP bias
                                                                
        V_CC=3.3V                            V_CC=3.3V          
           │                                    │               
        R_HI=470kΩ      ← base bias          R_LO=680Ω          
           │              (small I_B)           │               
           B                                    E               
           │                                    │               
           T  (BJT)                             T  (BJT)        
           │                                    │               
           E                                    B               
           │                                    │               
        R_LO=680Ω       ← emitter R          R_HI=470kΩ          
           │              (I_E sense)           │               
          GND                                  GND               
                                                                
        Collector also tied to V_CC          Collector also tied
        through R_LO=680Ω.                   to GND through R_LO.
```

Why these specific resistors?

- **`R_HI = 470 kΩ` on the base.** Across a ~2.6 V drop (V_CC minus a
  ~0.7 V base sit-point) this delivers a base current of about
  **(3.3 − 0.7) / 470 000 ≈ 5.5 µA** — small enough that even a low-β part
  produces an unambiguous collector current, large enough to be reliably
  above leakage.
- **`R_LO = 680 Ω` on collector and emitter.** Sized so that the I_C and
  I_E that follow from a 100× β (the order of magnitude for a small-signal
  part at this bias) drop ~0.4 V across the resistor — easily measured by
  the ADS1115, and well below the rail so the transistor stays in active
  region, not saturation.

These match the resistors physically wired around the test socket; the
constants `R_LO`, `R_HI`, and `V_CC` in `identify.cpp` are the
mathematical names for the same components.

---

## 6. Computing currents and β <a id="beta-formula"></a>

Once biased, we read three voltages with the ADS1115:
**V_B**, **V_C**, **V_E** (the voltages on the BJT's three terminals
relative to GND). Each bias resistor then gives us a current by **Ohm's
law**.

For an NPN with the network above:

| Current | Formula | Physical meaning |
|---|---|---|
| `I_B` | `(V_CC − V_B) / R_HI` | current through the 470 kΩ base resistor |
| `I_C` | `(V_CC − V_C) / R_LO` | current through the 680 Ω collector resistor |
| `I_E` | ` V_E       / R_LO` | current through the 680 Ω emitter resistor |
| `β`   | `I_C / I_B`           | DC current gain, also written **h_FE** |

For a PNP, the rails swap: every `V_CC − V_x` becomes `V_x` and vice
versa, because the resistor's other end is now at GND. The mirror-symmetric
form is in `computeBeta()`.

A useful sanity check (not currently asserted by the firmware): in the
active region **I_E ≈ I_B + I_C** by Kirchhoff's current law. If the
measurement obeys this to within a few percent we are likely in the active
region; large violations suggest leakage, saturation, or a bad contact.

References:
- All About Circuits — [Common-emitter amplifier](https://www.allaboutcircuits.com/textbook/semiconductors/chpt-4/common-emitter-amplifier/)
- Electronics Tutorials — [Transistor as an Amplifier](https://www.electronics-tutorials.ws/amplifier/amp_2.html)

---

## 7. The saturation guard <a id="saturation"></a>

A BJT has three operating regions:

| Region | Condition (NPN) | Behaviour |
|---|---|---|
| **Cut-off** | V_BE below ~0.5 V | Both junctions off. I_C ≈ 0. |
| **Active** | B–E forward, B–C reverse | I_C ≈ β·I_B. Useful for amplification and for our β measurement. |
| **Saturation** | Both junctions forward; V_CE small | I_C limited by external circuit, not by β. The β you'd compute is meaningless. |

We don't want to report β from a saturated measurement. The simplest test:
in active region V_CE is at least a few hundred mV. So we guard:

```cpp
float vCE = npn ? (vC - vE) : (vE - vC);
if (vCE < V_CE_SAT_MIN) return 0.0f;
```

`V_CE_SAT_MIN = 0.2 V` is the threshold. Below it we return β = 0 and let
the outer logic decide what to do (currently: prefer the other (C, E)
orientation, or report `Reason::BETA_TOO_LOW`).

References:
- All About Circuits — [BJT regions of operation](https://www.allaboutcircuits.com/textbook/semiconductors/chpt-4/active-mode-operation-bjt/)
- Wikipedia — [Bipolar junction transistor § Regions of operation](https://en.wikipedia.org/wiki/Bipolar_junction_transistor#Regions_of_operation)

---

## 8. V_BE: the final measurement <a id="vbe"></a>

Once we know which lead is which and the device is real, we directly read
the **base-emitter forward voltage** by:

- Pulling the base HIGH (through 680 Ω) and the emitter LOW (through 680 Ω)
  for an NPN; rails reversed for PNP.
- Releasing the collector.
- Reading V_B − V_E (NPN) or V_E − V_B (PNP).

With no collector path, the only current flowing is through the B–E
junction, so the measured V_BE is very close to the diode's intrinsic
forward drop at that current — typically 0.55 V at the few-mA bias this
network establishes. Datasheet V_BE values are usually quoted at higher
currents (1–10 mA), so expect the reported number to be slightly lower
than a datasheet figure for the same part.

---

## 9. Why this differs from a datasheet <a id="caveats"></a>

A few honest caveats so users don't get surprised:

- **β is current-dependent.** Datasheets typically specify h_FE at
  I_C = 1–10 mA. Our bias delivers ~150 µA collector current; β at that
  current can easily differ from datasheet values by 30% or more, in
  either direction. This is a property of the part, not a tester defect.
- **Resistor tolerance directly affects accuracy.** If your `R_LO`
  resistors are actually 700 Ω instead of 680 Ω, every reported current is
  off by 3%. Measure your actual resistors and update the constants if
  you want better-than-10% accuracy.
- **Single-point measurement.** We compute β at one operating point. The
  P3 curve-tracer phase will give the full I_C vs V_CE family.

See `transistor-tester-spec.md` §6 for the long-form discussion.

---

## See also
- [glossary.md](glossary.md) — definitions and external links for every
  abbreviation and term used here.
- [code-review-pedagogy.md](code-review-pedagogy.md) — the review that
  motivated the renames and comments in `identify.cpp`.
- [../transistor-tester-spec.md](../transistor-tester-spec.md) — overall
  product/instrument specification, including future phases.
