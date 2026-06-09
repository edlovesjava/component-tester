# ESP32 Transistor Tester & Curve Tracer — Design Document

> Adapted for standard ESP32 DevKit (ESP32-D0WD-V3) with 30-pin headers,
> driving an SPI TFT display and ADS1115 external ADC. Built with PlatformIO + Arduino-ESP32.

## Hardware

### Bill of Materials

| Qty | Item | Notes |
|-----|------|-------|
| 1 | ESP32 DevKit (ESP32-D0WD-V3, 38-pin) | MCU |
| 1 | ILI9341 SPI TFT 320×240 (1.5"–2") | Display |
| 1 | ADS1115 16-bit I2C ADC module | Measurement ADC |
| 3 | 680 Ω resistor | Test-pin low-R drive |
| 3 | 470 kΩ resistor | Test-pin high-R sense |
| 1–2 | R_sense (e.g. 100 Ω, 1 kΩ) | Collector current sense |
| — | RC filter parts (≈1 kΩ + 100 nF) ×2 | PWM smoothing for bias/sweep |
| 1 | MCP6002 (or similar RR op-amp) | Collector-sweep buffer (P3) |
| 1 | 14-pin ZIF socket | DUT connector |
| 1 | Rotary encoder + button | UI input (P3 optional) |
| — | Optional 5–12 V collector rail | Real V_CE range (P3) |
| — | Breadboard, jumpers, decoupling caps | — |

### Pin Map (ESP32 DevKit)

| Function | Signal | GPIO |
|----------|--------|------|
| I2C (ADS1115) | SDA / SCL | 21 / 22 |
| TFT SPI (VSPI) | SCK / MOSI / MISO | 18 / 23 / 19 |
| TFT control | CS / DC / RST | 5 / 17 / 16 |
| Test pin T1 | 680 Ω / 470 kΩ | 4 / 13 |
| Test pin T2 | 680 Ω / 470 kΩ | 14 / 27 |
| Test pin T3 | 680 Ω / 470 kΩ | 26 / 25 |
| Base current PWM | LEDC → RC filter → Rb | 32 |
| Collector sweep PWM | LEDC → RC filter → buffer | 33 |

### Test-Pin Topology

Each test pin (T1/T2/T3) connects to:
1. An **ADS1115 analog input** (voltage sense, A0–A2).
2. A **680 Ω resistor** to an ESP32 GPIO (low-impedance drive).
3. A **470 kΩ resistor** to an ESP32 GPIO (high-impedance weak pull).

By driving GPIOs high/low/Hi-Z in permutations and reading voltages, the firmware deduces junction polarity, device type, and pinout.

## Build Phases

| Phase | Deliverable | Stimulus | Notes |
|-------|-------------|----------|-------|
| **P1 — Identify** | Type (NPN/PNP) + pinout + single-point hFE / V_BE | Fixed drive | Proves permutation algorithm and front end |
| **P2 — Characterize** | hFE across I_C range; (I_b, I_C, V_CE) at fixed collector rail | Stepped I_b via PWM | No V_CE sweep yet |
| **P3 — Curve tracer** | Full I_C–V_CE family, color per I_b step, region tints, readout panel | Stepped I_b + swept V_CE | Needs op-amp buffer + higher collector rail |
| **P4 — Stretch** | BLE/ESP-NOW logging, MOSFET/JFET support, touch UI | — | Out of scope for initial build |

## Firmware Architecture

```
/src
  main.cpp               # setup + top-level state machine loop
  hal/
    pins.h               # GPIO constants (the only board-specific file)
    adc.cpp/h            # ADS1115 wrapper (Adafruit_ADS1X15)
    stimulus.cpp/h       # LEDC PWM → bias/sweep voltages
    testpins.cpp/h       # drive/float/sense T1/T2/T3 permutations
  measure/
    identify.cpp/h       # type + pinout detection (permutation probing)
    params.cpp/h         # hFE, V_BE at an operating point
    curves.cpp/h         # I_C-V_CE family capture
  render/
    display.h            # abstract: drawText / drawCurves / drawReadout
    display_tft.cpp      # TFT_eSPI implementation
  ui/
    input.cpp/h          # rotary encoder + button (optional)
```

### Key Libraries (PlatformIO)

- `arduino-esp32` framework
- `TFT_eSPI` (Bodmer) — TFT rendering
- `Adafruit ADS1X15` — ADS1115 driver

### Measurement Loop Pattern

```
set stimulus (PWM duty / pin state)
→ delay settle (RC τ × 5 + margin)
→ sample ADS1115 (average N reads)
→ record (I_b, V_BE, V_CE, I_C)
→ next step
```

### Identification Algorithm

1. Drive all 3 test pins through all combinations of H/L/Hi-Z
2. Measure resulting voltages on ADS1115
3. Detect junctions: forward drop ≈ 0.6–0.7 V one way, open the other
4. Base = pin common to two junctions (common anode = NPN, common cathode = PNP)
5. Distinguish C vs E: higher β in correct orientation, single-digit β when swapped

### Display Layout (TFT)

- Plot area on left (~240 px) for I_C–V_CE curves
- Readout panel on right: type, pinout, hFE, V_BE, cursor (V_CE, I_C)
- One color per I_b step, keyed to legend
- Region tints (saturation / active / cutoff)

## Pin Map Notes

- GPIO5 (TFT CS): strapping pin on ESP32 — safe to use as output after boot
- GPIO21/22: default I2C pins, ADC2 channels — no conflict since ADS1115 handles all analog
- GPIO19 (MISO): included for TFT_eSPI read support; optional if not needed
- TFT RST (GPIO16): can be tied to EN or 3.3 V if pin needed elsewhere
