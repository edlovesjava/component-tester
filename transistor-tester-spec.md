# ESP32 Transistor Tester & Curve Tracer — Specification

> A bench instrument that identifies a through-hole transistor's type and pinout,
> measures its parameters, and plots its output characteristic curves on a color
> display. Built around an ESP32 with an external precision ADC.
>
> **Framing:** this is *not* a clone of the $3 AliExpress component tester. The
> value-add over the classic AVR design is (a) a graphical, color curve tracer
> instead of a single hFE readout, and (b) optional connectivity — logging every
> measured part to a database over BLE / ESP-NOW.

---

## 1. Goals & non-goals

**Goals (v1):**
- Auto-detect device type: NPN / PNP BJT (MOSFET/JFET/diode are stretch goals).
- Auto-detect pinout — which physical pin is collector, base, emitter.
- Measure hFE (DC current gain) and V_BE.
- Plot the output-characteristic family: I_C vs V_CE for several stepped base currents, in color.
- Decoupled rendering layer so the display can be swapped without touching the measurement core.

**Non-goals (v1):**
- Passive measurement (R / L / C / ESR). The classic tester does this; we may add it later, but it is out of scope for the first build.
- High-voltage / high-current parts. Test currents stay modest (see §4 headroom note).
- Metrology-grade absolute accuracy. We want repeatable, sensible numbers, not a calibrated lab reference.

---

## 2. Build phases

The display is the *last* hard thing — it's just rendering. The real engineering is
the analog front end and the measurement algorithms. De-risk those first.

| Phase | Deliverable | Display | Stimulus | Notes |
|-------|-------------|---------|----------|-------|
| **P1 — Identify** | Type + pinout + single-point hFE / V_BE | 128×64 OLED (text) | Fixed | Proves the permutation algorithm and front end |
| **P2 — Characterize** | hFE across a range of I_C; (I_b, I_C, V_CE) points at a fixed collector rail | OLED or TFT | Stepped I_b | No V_CE sweep yet |
| **P3 — Curve tracer** | Full I_C–V_CE family, color per I_b step, region tints, readout panel | TFT (ILI9341) | Stepped I_b + swept V_CE | Needs a stiff collector source (see §4.4) |
| **P4 — Connected** | Log parts to a DB over BLE/ESP-NOW; touch UI; MOSFET/JFET/diode support | TFT + touch | — | Stretch |

Prototype P1–P2 on the 0.96" OLED already on hand; drop in the TFT at P3 as a pure rendering swap.

---

## 3. Hardware overview

```
                 +-------------------+
   USB / 5V  --->|  ESP32 (S3 rec.)  |
                 |                   |--- SPI ----> ILI9341 320x240 TFT  (P3+)
                 |                   |--- I2C ----> ADS1115 16-bit ADC   (sense)
                 |                   |--- I2C ----> SSD1306 OLED         (P1 proto)
                 |                   |--- PWM ----> RC filter ---+ (stimulus, see 4.3)
                 +-------------------+                           |
                                                                 v
                         3x test pins  T1 / T2 / T3  --[ analog front end, 4.2 ]
                                            |
                                      ZIF socket (DUT)
```

### 3.1 MCU
- **Recommended:** ESP32-S3-DevKitC (ample GPIO, PSRAM, fast SPI for the TFT).
  A classic ESP32 DevKitC is equally fine.
- The Xiao ESP32-S3 works if the pin count is sufficient, but a full DevKit is
  easier for a bench build.
- **DAC note:** we do **not** use the on-chip DAC, so the lack of a DAC on the
  S3 / C6 / H2 is irrelevant. (Only the classic ESP32 and S2 have a DAC anyway.)

### 3.2 Display
- **Primary (P3+):** ILI9341 2.4"–2.8" SPI TFT, 320×240, driven by **TFT_eSPI**
  (Bodmer). Sprite support for flicker-free partial redraws.
- **Prototype (P1–P2):** SSD1306 128×64 I2C OLED.
- Color is *functional*, not cosmetic: output curves bunch near the origin and a
  one-bit display can't separate them. One hue per I_b step is the whole reason
  for the TFT.

### 3.3 Measurement ADC
- **ADS1115** — 16-bit, 4 channels, I2C (default addr `0x48`), PGA.
- Use the ±4.096 V FSR setting at a 3.3 V rail → ~125 µV/LSB, plenty for mV-level V_BE.
- Throughput (~860 SPS max, less with channel switching) is fine for step-settle-measure;
  it is *not* fast enough for real-time sweeps, which is acceptable — the tracer
  steps, settles, then samples.
- **Why external:** the ESP32's internal SAR ADC is nonlinear, noisy, and needs
  per-chip calibration. Fine for qualitative detection (P1), poor for trustworthy
  numbers (P2+). The internal ADC is an acceptable fallback only for P1.

---

## 4. Analog front end

### 4.1 Test-pin topology (borrowed from the classic AVR tester)
Three test pins, T1/T2/T3. Each pin connects to **all three** of:
1. An **ADS1115 input** (voltage sense).
2. A **680 Ω** resistor to an ESP32 GPIO (low-impedance drive / current source-sink).
3. A **470 kΩ** resistor to an ESP32 GPIO (high-impedance drive, for leakage / weak pull).

By driving the GPIOs high / low / input (Hi-Z) in all permutations and reading the
resulting pin voltages, the firmware deduces junctions, polarity, and device type.
The 680 Ω resistors also provide current limiting against a charged cap shoved into the socket.

### 4.2 Identification & pinout algorithm
- A junction reads a forward drop (~0.6–0.7 V) one way, open the other.
- The **base** is the pin common to two junctions. For an NPN the base is the
  common anode; for a PNP the common cathode — this gives polarity.
- **C vs E** is the subtle part: the B-E junction forward drop is slightly *higher*
  than B-C, and (definitively) the device shows high forward β in the correct
  C/E orientation and collapses to single-digit reverse β when swapped. The
  firmware tries both assignments and picks the high-gain one.
- This auto-pinout result feeds the curve tracer's pin-role assignment in §4.4.

### 4.3 Stimulus — no DAC required
- **Base current** is set by a known voltage through a known resistor:
  `I_b = (V_drive − V_BE) / R_b`. Low current (µA), so an RC-filtered PWM
  (ESP32 LEDC) directly into R_b is stiff enough. Stepping the PWM duty steps I_b.
- LEDC gives high-bit-depth PWM; an RC filter (e.g. 1 kΩ + 100 nF, tune for ripple
  vs settling) turns it into a smooth bias voltage. Step → settle → measure means
  RC lag is a non-issue.
- This is why the **MCP4725 external DAC is not needed.** Add one only if a future
  revision wants buttery live sweeps.

### 4.4 Collector sweep (P3) — the real design decision
Getting a full I_C–V_CE curve at constant I_b requires sweeping V_CE while the
collector sources **mA**, which an RC-filtered PWM cannot supply stiffly. Options,
documented so we choose deliberately:

- **P2 simplification:** fix the collector rail through a sense resistor R_sense and
  step only I_b. You get one operating point per I_b (good for hFE-vs-I_C), not a
  full curve. No buffer needed.
- **P3 full curves:** buffer the swept voltage with a rail-to-rail op-amp
  (e.g. MCP6002) or a pass transistor so the collector node can source current
  while V_CE is swept. **This is the gating analog task for P3.**
- **Collector supply rail:** at 3.3 V the usable V_CE range is tiny once R_sense
  takes its share. For a meaningful x-axis, give the *collector* its own higher rail
  (5–12 V, separate supply or small boost) while keeping ESP32 logic at 3.3 V. Logic
  stays safe; the DUT gets real headroom.
- **Current sense:** `I_C = V_Rsense / R_sense`. Pick R_sense for the target current
  range (e.g. 100 Ω → 2 V at 20 mA). Remember it steals V_CE headroom — keep test
  currents modest (sub-10 mA) unless using the higher collector rail.

### 4.5 Headroom caveat (3.3 V logic)
BJTs and logic-level MOSFETs are fully testable. High-V_GS(th) power MOSFETs and
depletion devices can't be fully driven from 3.3 V — out of scope for v1. The
optional collector rail in §4.4 addresses V_CE range but not gate-drive voltage.

### 4.6 DUT connector
14-pin **ZIF socket** (zero insertion force) or spring terminals — no soldering of
the part under test, fits the breadboard-friendly workflow.

---

## 5. Suggested pin map (adapt to your board)

> Illustrative for ESP32-S3. Avoid strapping / input-only pins for outputs;
> verify against your specific board's pinout.

| Function | Signal | Pin (example) |
|----------|--------|---------------|
| I2C (ADS1115 + OLED) | SDA / SCL | GPIO8 / GPIO9 |
| TFT SPI | SCK / MOSI | GPIO12 / GPIO11 |
| TFT control | DC / CS / RST / BL | GPIO13 / GPIO10 / GPIO14 / GPIO21 |
| Test pin T1 drive | 680 Ω / 470 kΩ GPIO | GPIO4 / GPIO5 |
| Test pin T2 drive | 680 Ω / 470 kΩ GPIO | GPIO6 / GPIO7 |
| Test pin T3 drive | 680 Ω / 470 kΩ GPIO | GPIO15 / GPIO16 |
| Base-current PWM | LEDC out → RC | GPIO17 |
| Collector-sweep PWM | LEDC out → RC → buffer | GPIO18 |
| UI (optional) | Encoder A/B + button | GPIO1 / GPIO2 / GPIO42 |
| Sense (ADS1115) | A0–A2 = T1–T3, A3 = R_sense / ref | — |

---

## 6. Display & UI

- **Layout (TFT):** plot area on the left (~240 px wide), readout panel on the right.
- **Curves:** one color per I_b step; legend keyed to color.
- **Region tints:** faint colored fills under the curves for saturation / active /
  cutoff regions — the pedagogical payoff, only feasible in color.
- **Readout panel:** type, pinout (C/B/E mapped to physical pins), hFE, V_BE, and the
  cursor's (V_CE, I_C) at a selected point.
- **Pixel mapping:** `x = x0 + (V_CE / V_CE_max) * plot_w`, `y = y0 + plot_h − (I_C / I_C_max) * plot_h`.
  Auto-range V_CE_max / I_C_max from the captured data, round to nice limits.
- **UI input:** rotary encoder + button (P3) or resistive touch (XPT2046, shares SPI) at P4.

---

## 7. Firmware architecture

Keep the measurement core hardware- and display-agnostic.

```
/src
  main.cpp              # setup + top-level state machine
  hal/
    pins.h              # pin map (the only board-specific file)
    adc.{h,cpp}         # ADS1115 wrapper; internal-ADC fallback behind same API
    stimulus.{h,cpp}    # LEDC PWM -> bias/sweep voltages; set_Ib(), sweep_Vce()
    testpins.{h,cpp}    # drive/float/sense the 3 test pins
  measure/
    identify.{h,cpp}    # type + pinout (permutation probing)  -- P1
    params.{h,cpp}      # hFE, V_BE at an operating point      -- P1/P2
    curves.{h,cpp}      # capture I_C-V_CE family               -- P3
  render/
    display.h           # abstract: drawText/drawCurves/drawReadout
    display_oled.cpp    # SSD1306 impl  -- P1/P2
    display_tft.cpp     # ILI9341 impl  -- P3+
  ui/
    input.{h,cpp}       # encoder / touch
  comms/
    logger.{h,cpp}      # BLE / ESP-NOW part logging  -- P4
```

- **Build env:** Arduino-ESP32 is the fastest path (TFT_eSPI + Adafruit_ADS1X15
  are mature there). ESP-IDF is viable if P4 connectivity gets heavy.
- **Key libraries:** `TFT_eSPI` (Bodmer), `Adafruit_ADS1X15`, `Adafruit_SSD1306`
  (proto), ESP-NOW / NimBLE (P4).
- **Measurement loop pattern:** set stimulus → wait for settle (RC time constant
  + margin) → sample ADS1115 (average N reads) → record → next step.

---

## 8. Calibration & accuracy notes
- Calibrate the ADS1115 reading against a known reference once; store offset/scale in NVS.
- Measure actual resistor values (680 Ω, 470 kΩ, R_sense) and use the measured
  values in the math — tolerance dominates accuracy here.
- If the internal ADC is ever used (P1 fallback), enable eFuse Vref calibration and
  expect only qualitative trustworthiness.

---

## 9. Bill of materials (v1)

| Qty | Item | Notes |
|-----|------|-------|
| 1 | ESP32-S3-DevKitC (or classic ESP32 DevKitC) | MCU |
| 1 | ILI9341 2.4"/2.8" SPI TFT 320×240 | P3+; XPT2046 touch optional |
| 1 | SSD1306 0.96" 128×64 I2C OLED | Prototyping (P1/P2) |
| 1 | ADS1115 16-bit I2C ADC module | Measurement |
| 3 | 680 Ω resistor | Test-pin low-R |
| 3 | 470 kΩ resistor | Test-pin high-R |
| 1–2 | R_sense (e.g. 100 Ω, 1 kΩ) | Collector current sense; value per range |
| — | RC filter parts (≈1 kΩ + 100 nF) ×2 | PWM smoothing |
| 1 | MCP6002 (or similar RR op-amp) | Collector-sweep buffer (P3) |
| 1 | 14-pin ZIF socket | DUT connector |
| 1 | Rotary encoder + button | UI (or use touch) |
| — | Optional 5–12 V collector rail (boost or supply) | Real V_CE range (P3) |
| — | Decoupling caps, protoboard, jumpers | — |

---

## 10. Open questions / decisions to revisit
1. **Collector sweep source (P3):** op-amp buffer vs pass transistor vs programmable
   current source — pick when starting P3 (§4.4).
2. **Collector rail voltage:** stay at 3.3 V (tiny V_CE range) or add a 5–12 V rail?
   Recommend the higher rail for a useful tracer.
3. **R_sense value(s):** single fixed vs switched ranges for low/high current parts.
4. **UI:** encoder vs touch — touch is nicer but adds an SPI device and complexity.
5. **P4 transport:** BLE to phone vs ESP-NOW into the existing mesh vs both.

---

## 11. References
- Markus Frejek / Karl-Heinz Kübbeler "AVR Transistor / Component Tester" — the
  canonical 3-pin / 680 Ω / 470 kΩ permutation approach this front end is based on.
- TFT_eSPI library (Bodmer) — TFT rendering on ESP32.
- ADS1115 datasheet — PGA settings, data-rate vs noise tradeoff.
