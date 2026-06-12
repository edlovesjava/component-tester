# Component Tester

A transistor identification and parameter measurement tool built on the ESP32 DevKit with a 128x160 color TFT display and ADS1115 16-bit ADC.

## Features

- **Identification**: Detects NPN/PNP type and pinout (C/B/E) automatically
- **hFE measurement**: DC current gain at ~150 µA collector current
- **V_BE measurement**: Base-emitter forward voltage drop
- **I_E measurement**: Emitter current
- **Graphical display**: Transistor symbol with pin labels and measurement readout on a 128x160 ST7735 TFT

## Hardware

### Bill of Materials

| Item | Part |
|------|------|
| MCU | ESP32-D0WD-V3 DevKit (38-pin) |
| Display | 1.8" ST7735 128x160 SPI TFT |
| ADC | ADS1115 16-bit I2C @ 0x48 (ADDR → GND) |
| USB-serial | CP210x on COM16 |

### Test Pin Circuit

Each of the three test nodes (T1–T3) has a dual GPIO drive:

| Node | LO GPIO | HI GPIO | ADC Chan | ZIF |
|------|---------|---------|----------|-----|
| T1 | GPIO4 (680Ω) | GPIO13 (470kΩ) | A0 | 1 |
| T2 | GPIO14 (680Ω) | GPIO27 (470kΩ) | A1 | 2 |
| T3 | GPIO26 (680Ω) | GPIO25 (470kΩ) | A2 | 3 |

- **LO pin**: drives the node low through 680Ω, or to VCC through 680Ω
- **HI pin**: pulls the node to VCC through 470kΩ (weak pull-up), or to GND through 470kΩ
- **ADC**: reads node voltage via the ADS1115

Two additional PWM-output GPIOs pass through RC filters for swept-parameter characterization:

| PWM | GPIO | Purpose |
|-----|------|---------|
| BASE_PWM | GPIO32 | Base bias voltage |
| COLLECTOR_PWM | GPIO33 | Collector supply voltage |

### Other Connections

| Function | Pins |
|----------|------|
| I2C (ADS1115) | SDA=GPIO21, SCL=GPIO22 |
| TFT SPI (VSPI) | SCLK=18, MOSI=23, MISO=19, CS=5, DC=17, RST=16 |

## Software

### Framework

- **PlatformIO** with **Arduino-ESP32** framework
- **TFT_eSPI** for display control (custom config in `lib/custom_tft_setup/tft_setup.h`)
- **Adafruit ADS1X15** for ADC readings

### Architecture (`src/`)

```
src/
├── main.cpp                  # Entry point, display loop
├── hal/
│   ├── pins.h                # GPIO/I2C/SPI pin assignments
│   ├── adc.h / adc.cpp       # ADS1115 wrapper
│   ├── testpins.h / testpins.cpp  # Test pin GPIO drive
│   └── stimulus.h / stimulus.cpp  # PWM stimulus for characterization
└── measure/
    ├── identify.h            # Identification types and interface
    └── identify.cpp          # Probe logic and measurement
```

### How Identification Works

1. **Forward probe**: For each test pin (driven HIGH through 680Ω), measure voltage drop to the other two pins (pulled LOW through 680Ω). A forward-biased junction reads ~0.3–0.9V.

2. **Reverse probe**: For each test pin (driven LOW), measure voltage rise from the other pins (pulled HIGH through 470kΩ). A reverse-biased junction reads ~0.3–0.9V.

3. **Base detection**: If forward probing a pin shows junctions to both others → it's the base of an NPN. If reverse probing a pin shows junctions from both others → it's the base of a PNP.

4. **hFE measurement**: The identified base is biased through 470kΩ, the collector through 680Ω, and the emitter is grounded. Voltages are read and hFE = I_C / I_B is computed.

5. **V_BE measurement**: With the base forward-biased and emitter grounded, the base-emitter voltage is read directly.

## Schematics

### Single Test Node

```
VCC (3.3V)
  │
  ├── GPIO_HI ── 470kΩ ──┐
  │                       ├── ADC ── ZIF pin
  ├── GPIO_LO ── 680Ω ───┘
  │
 GND
```

### Transistor Under Test

```
 T1 ──────────┐
              │
 T2 ──────────┤── ZIF socket ── Device Under Test
              │
 T3 ──────────┘
```

## Development

### Build & Upload

```bash
pio run -t upload --upload-port COM16
```

### Serial Monitor

```bash
pio device monitor -p COM16 -b 115200
```

### Output Format

On valid identification, serial output:
```
NPN C=1 B=2 E=3 hFE=167 Ie=0.584 Vbe=0.485
```

TFT display shows the transistor symbol with C/B/E pin labels and measured values (hFE, Ie, Vbe).

## Caveats

- **Measurement accuracy**: The 680Ω emitter/collector resistors may deviate from nominal (measured ~2.73kΩ on some builds), affecting hFE and Ie accuracy. Calibrate current-sense constants in `identify.cpp` to match actual resistor values.
- **Current range**: hFE is measured at a low collector current (~150 µA) and may differ from datasheet values specified at higher currents.
- **clangd on Windows**: The xtensa-esp32-elf toolchain is not fully supported by clangd on Windows. The "expected function body after function declarator" error from framework headers is harmless. Use the post-processed `compile_commands.json` for LSP.

## Documentation

- [`docs/theory.md`](docs/theory.md) — how identification works in physical
  terms (junctions, base detection, bias network, β formula, saturation).
  Read this first if you want to understand the firmware.
- [`docs/glossary.md`](docs/glossary.md) — every abbreviation and term of
  art used in the code and docs, with beginner/intermediate/advanced
  external links per concept (BJT, NPN/PNP, diode, h_FE, V_BE, Hi-Z,
  Thévenin source, etc.).
- [`docs/code-review-pedagogy.md`](docs/code-review-pedagogy.md) — the
  review notes that motivated the comment / naming choices in
  `src/measure/identify.cpp`.
- [`transistor-tester-spec.md`](transistor-tester-spec.md) — full
  instrument specification including future phases.

## License

MIT
