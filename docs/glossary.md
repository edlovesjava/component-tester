# Glossary & Further Reading

> Every abbreviation and term-of-art used in this codebase, with a one-line
> definition followed by curated external links. The reading level on each
> link is tagged:
>
> - **\[basic]** — assumes no prior electronics background; safe starting point.
> - **\[intermediate]** — assumes Ohm's law, basic diodes, basic transistors.
> - **\[advanced]** — for the reader who already knows the topic and wants
>   depth or rigor.
>
> Where a term is implemented by a specific constant or function in this
> repo, the code reference appears in `monospace`.

---

## Devices

### BJT — Bipolar Junction Transistor
A three-terminal semiconductor device with two PN junctions (N–P–N or
P–N–P). Acts as a current-controlled current source in its active region.
This whole project measures BJTs.

- \[basic] SparkFun — [Transistors](https://learn.sparkfun.com/tutorials/transistors)
- \[basic] Khan Academy — [BJT introduction (video)](https://www.khanacademy.org/science/electrical-engineering/ee-semiconductor-devices/ee-bipolar-junction-transistors/v/ee-bjt-introduction)
- \[intermediate] All About Circuits — [BJT chapter](https://www.allaboutcircuits.com/textbook/semiconductors/chpt-4/bipolar-junction-transistors-bjt/)
- \[advanced] Wikipedia — [Bipolar junction transistor](https://en.wikipedia.org/wiki/Bipolar_junction_transistor)

### NPN
A BJT with N-doped collector and emitter, and a P-doped base. Conventional
collector current flows from collector to emitter when the base is biased
above the emitter (so V_BE > 0). Arrow on the emitter symbol points
**out** ("**N**ot **P**ointing i**N**").
See [theory.md §1](theory.md#bjt-overview).

- \[basic] Electronics Tutorials — [The NPN Transistor](https://www.electronics-tutorials.ws/transistor/tran_2.html)

### PNP
The mirror of NPN. P-doped collector and emitter, N-doped base. Currents
and voltages are inverted: V_BE < 0, V_CE < 0, I_C flows into the emitter
and out of the collector. Arrow on the emitter symbol points **in**.

- \[basic] Electronics Tutorials — [The PNP Transistor](https://www.electronics-tutorials.ws/transistor/tran_3.html)

### Diode
A two-terminal device made of a single PN junction. Conducts current in
one direction (anode → cathode) with a small forward voltage (~0.6 V for
Si, ~0.3 V for Ge / Schottky). The BJT identification algorithm models
each B–E and B–C junction as a diode — see [theory.md §1](theory.md#bjt-overview).

- \[basic] SparkFun — [Diodes](https://learn.sparkfun.com/tutorials/diodes/all)
- \[intermediate] Wikipedia — [P–n junction](https://en.wikipedia.org/wiki/P%E2%80%93n_junction)
- \[advanced] HyperPhysics — [The P-N Junction](http://hyperphysics.phy-astr.gsu.edu/hbase/Solids/pnjun.html)

### MOSFET
"Metal-Oxide-Semiconductor Field-Effect Transistor." A different family of
transistor with a voltage-controlled channel rather than current-controlled.
**Out of scope for v1 of this tester.** The MOSFET design spec is in
`docs/MOSFET-design.md` (planned).

- \[basic] SparkFun — [Transistors (MOSFET section)](https://learn.sparkfun.com/tutorials/transistors)

---

## Terminals

### B / Base
The middle, thin region of the BJT silicon sandwich. The terminal that
controls the device: a small base current produces a large collector
current. Detected by the algorithm as the pin that connects to the other
two through forward-biased junctions — see [theory.md §3](theory.md#base-detection).

### C / Collector
The terminal that "collects" the majority of the current carriers swept
across the base. Physically larger than the emitter and lightly doped.
Connected to V_CC through `R_LO = 680 Ω` during β measurement.

### E / Emitter
The terminal that "emits" the majority carriers into the base. Heavily
doped. Connected to GND through `R_LO = 680 Ω` during β measurement
(NPN; rails swap for PNP).

The asymmetry between C and E is the reason the algorithm tries both
candidate assignments and keeps the higher-β one — see
[theory.md §4](theory.md#collector-vs-emitter).

---

## Parameters we measure

### β / beta / h_FE / hFE
**DC current gain.** Defined as `β = I_C / I_B` at a specific operating
point. For a small-signal BJT at this tester's bias (~150 µA collector
current), typical values are ~50–500. `hFE` is the spec-sheet name for
the same quantity. Our code calls the variable `hfe` and the constants
`MIN_HFE`. See [theory.md §6](theory.md#beta-formula).

- \[basic] Adafruit — [What is hFE?](https://learn.adafruit.com/transistors-101)
- \[intermediate] Electronics Tutorials — [Transistor as an Amplifier](https://www.electronics-tutorials.ws/amplifier/amp_2.html)
- \[advanced] Sedra & Smith, *Microelectronic Circuits*, §6.2

### V_BE
**Base-emitter voltage.** The forward voltage across the B–E PN junction
when the device is conducting. For a silicon BJT, ~0.55–0.7 V depending on
current. Mildly temperature-dependent (~−2 mV/°C), which is the basis of
classical bandgap references. Measured by the firmware in the final step
of `identify()`.

- \[intermediate] All About Circuits — [The transistor as a switch](https://www.allaboutcircuits.com/textbook/semiconductors/chpt-4/the-transistor-as-a-switch/)

### V_CE
**Collector-emitter voltage.** In the active region this is roughly
`V_CC − I_C·R_LO − V_E`. Near zero, the device is in **saturation** and
β no longer applies — guarded by `V_CE_SAT_MIN = 0.2 V` in
`identify.cpp`. See [theory.md §7](theory.md#saturation).

### V_CC
**Supply rail voltage.** 3.3 V in this design — the ESP32 logic rail
also feeds the test pin Thévenin sources. Named `V_CC` in
`identify.cpp`.

### I_B, I_C, I_E
Base, collector, and emitter currents. Conventional direction (where
positive current flows) is into the device for NPN, out of the device for
PNP. **Kirchhoff's current law** demands `I_E = I_B + I_C` everywhere; we
use this as an implicit sanity check.

### Active region / Cut-off / Saturation
The three operating regions of a BJT. We measure β only in the **active
region**. See [theory.md §7](theory.md#saturation).

---

## Circuit / signal terms

### Forward bias / reverse bias
- **Forward**: anode higher than cathode; the diode conducts.
- **Reverse**: anode lower than cathode; the diode blocks (apart from
  leakage). Both used in the junction probing step
  ([theory.md §2](theory.md#diode-junction)).

### Hi-Z / high-impedance
A GPIO pin in input mode is "Hi-Z" — it presents a very high impedance to
the circuit and behaves as if it were disconnected. The firmware uses
`PinDrive::DRV_HIZ` to disable a drive path without removing the device.

### Thévenin source
A real-world voltage source modelled as an ideal voltage source in series
with a single resistor. Each test node is two Thévenin sources in
parallel: one through `R_LO = 680 Ω` (low impedance), one through
`R_HI = 470 kΩ` (high impedance). The `NodeRole` enum
(`STRONG_HIGH`, `WEAK_HIGH`, …) names the four useful combinations.

- \[intermediate] All About Circuits — [Thevenin's theorem](https://www.allaboutcircuits.com/textbook/direct-current/chpt-10/thevenins-theorem/)

### Ohm's law
`V = I · R`. We use it three times per β measurement to convert measured
terminal voltages into terminal currents — [theory.md §6](theory.md#beta-formula).

### Kirchhoff's current law (KCL)
The currents into a node sum to zero. For a BJT, `I_E = I_B + I_C`.

### PWM / Pulse-Width Modulation
A digital output that toggles between 0 and V_CC at a fixed frequency with a
controllable duty cycle. Followed by an RC filter, it produces an analog
voltage proportional to the duty cycle — used by `Stimulus` as a coarse
DAC for the P2+ swept-bias curve tracer.

- \[basic] SparkFun — [PWM](https://learn.sparkfun.com/tutorials/pulse-width-modulation)

### RC filter
A resistor + capacitor low-pass filter. Smooths the PWM stream into a
slow-moving analog voltage. Cut-off `f_c = 1 / (2π·R·C)`.

### DAC / Digital-to-Analog Converter
A circuit that converts a digital number into an analog voltage. The ESP32
has on-chip DACs on the classic ESP32 / S2; we **don't use them** — we
use PWM+RC instead, for portability across the ESP32 family.

### ADC / Analog-to-Digital Converter
The opposite of a DAC. Used here for measuring test node voltages.

---

## Specific components

### ADS1115
A 16-bit, 4-channel, I²C ADC by Texas Instruments. Used over the ESP32's
internal ADC because the internal SAR ADC is nonlinear and noisy. Default
I²C address `0x48`. Configured for `GAIN_ONE` → ±4.096 V FSR.

- [TI ADS1115 datasheet](https://www.ti.com/product/ADS1115)
- [Adafruit ADS1X15 library](https://github.com/adafruit/Adafruit_ADS1X15)

### ST7735
A small color TFT controller; our display is 128×160. Driven by the
[TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) library through the ESP32's
VSPI bus.

### ESP32
The microcontroller running this firmware (specifically an ESP32-D0WD-V3
DevKit). Dual-core Xtensa LX6, lots of GPIO, hardware I²C / SPI / PWM.

### LSB / FSR
- **LSB** — Least Significant Bit. One LSB is the smallest voltage the ADC
  can distinguish. For our ADS1115 setup, ≈ 125 µV.
- **FSR** — Full-Scale Range. The voltage range the ADC can read; here
  ±4.096 V.

### Schottky diode
A diode using a metal-semiconductor junction. Forward voltage ~0.2–0.4 V,
considerably lower than a silicon PN diode. Our `V_FWD_MIN = 0.3 V`
bound accepts Schottkys for identification purposes (although our actual
target devices are silicon BJTs).

### Germanium / Ge
The semiconductor used in early transistors. Forward junction voltage
~0.2–0.4 V, lower than silicon's 0.6–0.7 V. Mostly historical, but a few
audio and RF devices still use Ge. Again, `V_FWD_MIN` is set to admit
these.

---

## Bench / measurement terms

### Curve tracer
A bench instrument that plots the I_C–V_CE family of curves for a
transistor, one curve per stepped I_B value. P3 of this project. See
`transistor-tester-spec.md`.

### ZIF socket
"Zero Insertion Force" socket: a clamping socket that lets you insert and
remove DIP/TO-style parts without physical force on the leads. The
test pins T1/T2/T3 are wired to its top three positions.

### DUT
"Device Under Test." Standard bench shorthand for "whatever you currently
have plugged into the instrument."

### V_CE_SAT
The collector-emitter voltage in saturation, typically 0.1–0.3 V. Our
`V_CE_SAT_MIN = 0.2 V` is a threshold for *detecting* saturation, not for
representing V_CE_SAT itself.

---

## Software / harness terms

### PlatformIO (pio)
The cross-platform build system used in this repo. `pio run` builds,
`pio run -t upload` flashes the board.

### Arduino-ESP32
The Espressif port of the Arduino framework to the ESP32. Provides
`pinMode`, `digitalWrite`, `ledcWrite`, etc.

### TFT_eSPI
Bodmer's high-performance display driver library used for the ST7735.
Configured via `lib/custom_tft_setup/tft_setup.h`.

### LEDC
The ESP32's hardware PWM peripheral. The `Stimulus` class drives two LEDC
channels through RC filters as a poor man's DAC.

---

## See also
- [theory.md](theory.md) — narrative explanation of how the firmware
  detects and measures a BJT.
- [code-review-pedagogy.md](code-review-pedagogy.md) — review notes that
  motivated this glossary.
- [../README.md](../README.md) — project overview and hardware reference.
- [../transistor-tester-spec.md](../transistor-tester-spec.md) — long-form
  spec including the planned phases (curve tracer, MOSFET support, etc.).
