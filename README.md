# Lootr Audio Synth / Sampler Project

## Project Overview

Lootr is an interactive audio prototype where joystick direction selects an item
category and joystick amplitude controls selection tightness/intensity. The
project currently has two active tracks:

- Python prototype for gameplay/selection logic iteration.
- Raspberry Pi Pico 2020 hardware diagnostics for SD-card stability.

Primary hardware target is now Raspberry Pi Pico 2020 (RP2040).

## Current Primary Target (Raspberry Pi Pico 2020)

Use the Pico SD diagnostics sketch:

- `raspberry_pi_pico_sd_diag/raspberry_pi_pico_sd_diag.ino`

This sketch continuously verifies SD initialization and file enumeration and is
the canonical hardware bring-up path.

### Pico Wiring (SD Reader)

Use GPIO labels (not just physical position descriptions):

| SD Reader Pin | Pico GPIO | Pico Physical Pin    |
| ------------- | --------- | -------------------- |
| CS            | GP17      | 22                   |
| SCK           | GP18      | 24                   |
| MOSI / DI     | GP19      | 25                   |
| MISO / DO     | GP16      | 21                   |
| GND           | GND       | 23 (recommended)     |
| VCC           | 3V3 or 5V | 36 (3V3) / 40 (VBUS) |

Power guidance:

- Start with 3V3 power.
- If init fails consistently, test 5V VBUS only if module supports it.
- Keep MISO/DO voltage within Pico-safe logic range.

### Pico Board Package Requirement

This sketch expects the Earle Philhower RP2040 core in Arduino IDE.

Board Manager URL:

```text
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

### Expected Serial Output

Working wiring/module behavior shows lines like:

```text
CS=17 -> SD.begin OK, files=112, raw=112
[PASS N] CS=17 files=112 raw=112
```

Failure shows:

```text
No working CS pin found.
[FAIL N] no active CS
```

## Joystick Bring-Up (Serial Only)

If you want to verify joystick input without audio, use:

- `raspberry_pi_pico_joystick_diag/raspberry_pi_pico_joystick_diag.ino`

### Pico Wiring (KY-023)

| KY-023 Pin | Pico GPIO | Pico Physical Pin |
| ---------- | --------- | ----------------- |
| GND        | GND       | 23 (recommended)  |
| +5V        | 3V3       | 36                |
| VRx        | GP26      | 31                |
| VRy        | GP27      | 32                |
| SW         | GP15      | 20                |

Important:

- Power the joystick from Pico 3V3, not 5V.
- The button line is active LOW (`PRESSED` when grounded).

### Serial Test Steps

1. Upload joystick sketch.
2. Open Serial Monitor at 115200 baud.
3. Leave stick untouched for startup calibration.
4. Move stick and press button.

Expected stream:

```text
rawX=... rawY=... normX=... normY=... sw=released
rawX=... rawY=... normX=... normY=... sw=PRESSED
```

Commands in serial monitor:

- `c` recalibrates center (leave stick untouched)
- `h` prints help

## Setup & Running on Mac (Prototype)

1. Ensure Python 3 is installed.
2. Sync dependencies:

```bash
uv sync
```

3. Run prototype:

```bash
source .venv/bin/activate
python proto.py
```

## Legacy Teensy Support

Teensy 4.0 PlatformIO firmware remains in this repo as legacy compatibility and
reference material. It is no longer the primary bring-up path.

Legacy files include:

- `platformio.ini` Teensy environments
- `src/main.cpp`, `src/main_hw_test.cpp`, `src/main_sd_diag.cpp`
- `scripts/flash_*` helpers that call `pio run -e teensy40...`

Use these only if you are intentionally targeting Teensy hardware.
