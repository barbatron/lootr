# Lootr Audio Synth / Sampler Project

## Project Overview

Lootr is an interactive audio prototype where joystick direction selects an item
category and joystick amplitude controls selection tightness/intensity. The
project currently targets Raspberry Pi Pico 2020 (RP2040) bring-up for:

- SD asset loading
- KY-023 joystick input
- MAX98357A I2S audio playback

## Documentation

Structured project docs are in:

- `docs/README.md`
- `docs/local-testing.md`
- `docs/hardware-wiring.md`

## Current Primary Target (Raspberry Pi Pico 2020)

Use the Pico SD diagnostics sketch:

- `raspberry_pi_pico_sd_diag/raspberry_pi_pico_sd_diag.ino`

Integrated SD + joystick + MAX98357A playback sketch:

- `raspberry_pi_pico_lootr_integration/raspberry_pi_pico_lootr_integration.ino`

This is the canonical hardware bring-up path.

## VSCode + PlatformIO Workflow (Primary Dev Loop)

PlatformIO is configured for the integration sketch in
`raspberry_pi_pico_lootr_integration/`.

Typical loop:

```bash
pio run -e pico_lootr_integration
pio run -e pico_lootr_integration --target upload
./scripts/monitor.sh
```

Firmware upload helper:

```bash
./scripts/pio_upload.sh
```

Asset-count tuning without changing SD contents (compile-time sampling):

```bash
./scripts/pio_upload.sh --asset-sampling 0.70
```

`--asset-sampling` accepts values in `0.0..1.0`; lower values deterministically
skip more WAV files during indexing, which can reduce trigger-time overhead with
large SD libraries.

Quick diagnostics:

```bash
./scripts/pio_doctor.sh
./scripts/pio_doctor.sh --build
```

Port helper:

```bash
./scripts/find_pico_port.sh
```

Why your serial port changes (`cu.usbmodem103*` vs `cu.usbmodem104*`):

- macOS assigns those device suffixes dynamically.
- The value can change across reconnects, resets, and BOOTSEL transitions.
- It is not a stable indicator of which physical USB-C socket you used.

The PlatformIO config uses wildcard monitor-port matching to reduce manual
port re-selection.

### Pico Wiring (SD Reader)

Use GPIO labels (not just physical position descriptions):

| SD Reader Pin | Pico GPIO | Pico Physical Pin    |
| ------------- | --------- | -------------------- |
| CS            | GP17      | 22                   |
| SCK           | GP18      | 24                   |
| MOSI / DI     | GP19      | 25                   |
| MISO / DO     | GP16      | 21                   |
| GND           | GND       | 23                   |
| VCC           | 3V3 or 5V | 36 (3V3) / 40 (VBUS) |

Power notes:

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
CS=17 -> SD.begin OK, files=112, wav=112
[PASS N] CS=17 files=112 wav=112
```

Failure shows:

```text
No working CS pin found.
[FAIL N] no active CS
```

## Joystick Bring-Up (Serial Only)

To verify joystick input without audio, use:

- `raspberry_pi_pico_joystick_diag/raspberry_pi_pico_joystick_diag.ino`

### Pico Wiring (KY-023)

| KY-023 Pin | Pico GPIO | Pico Physical Pin |
| ---------- | --------- | ----------------- |
| GND        | GND       | 23                |
| +5V        | 3V3       | 36                |
| VRx        | GP26      | 31                |
| VRy        | GP27      | 32                |
| SW         | GP15      | 20                |

Important:

- Power the joystick from Pico 3V3, not 5V.
- The joystick SW line is active LOW (`PRESSED` when grounded).
- No separate trigger switch is used.

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

## Integrated Playback Bring-Up (SD + Joystick + MAX98357A)

Sketch:

- `raspberry_pi_pico_lootr_integration/raspberry_pi_pico_lootr_integration.ino`

Additional audio wiring for MAX98357A:

| MAX98357A Pin | Pico GPIO | Pico Physical Pin |
| ------------- | --------- | ----------------- |
| BCLK          | GP10      | 14                |
| LRC           | GP11      | 15                |
| DIN           | GP12      | 16                |
| SD            | GP13      | 17                |
| GND           | GND       | 23 or 38          |
| Vin           | VBUS / 5V | 40                |

Asset requirements on SD root:

- `.wav` files readable by AudioTools `WAVDecoder`
- 44100 Hz mono files are recommended to match sketch defaults
- Filenames should include material keywords for angle mapping (examples:
  `metal`, `stone`, `wood`)

Runtime serial commands at 115200:

- `c` recalibrate joystick center
- `r` rescan assets on SD
- `s` print status
- `p` play one short test chirp
- `v` toggle verbose per-trigger logs
- `l` toggle AudioTools logger level (`warning`/`info`)
- `z` reset runtime performance counters

Behavior:

- Hold joystick button and move stick direction to trigger playback.
- Joystick angle picks material category.
- Joystick amplitude changes selection spread (center = broad/random, edge =
  tighter).

### Offline asset preprocessing (silence trim + WAV/RAW export)

To reduce runtime cost and avoid long leading/trailing silence, preprocess
assets with ffmpeg:

```bash
./scripts/process_assets_ffmpeg.sh
```

This writes processed files to gitignored output folders:

- `.generated/processed-assets/wav/` (trimmed WAV, mono s16, 44.1kHz)
- `.generated/processed-assets/raw/` (trimmed RAW PCM s16le, mono, 44.1kHz)

Optional lower-rate test output (useful for RP2040 headroom checks):

```bash
./scripts/process_assets_ffmpeg.sh --sample-rate 22050
```

Startup status signaling (on `LED_BUILTIN` if present, plus chirps when I2S
audio is available):

- Booted and about to start SD work: single short blink/chirp.
- SD asset scan complete: double short blink/chirp.
- SD initialization or asset scan failed: long blink/chirp followed by short
  blink/chirp.
- MAX98357A / I2S init failed: long blink plus two short blinks (repeats while
  halted).
