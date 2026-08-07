# Lootr Audio Synth / Sampler Project

## Project Overview

An interactive audio application that plays categorized sound samples based on
joystick input. The project maps joystick X/Y coordinates to an angle and
amplitude to dynamically select and play different material/item sound effects.

Ultimately, this project is intended to be flashed onto hardware (such as a
Teensy 4.0/4.1 using PlatformIO). To iterate on the audio selection math and the
UX quickly, there is currently a Python prototype.

## Core Mechanics

1. **Inputs**:
   - X/Y Joystick: Angle determines the item category, amplitude determines
     volume/intensity.
   - Play Trigger: A button that enables playback.
2. **Playback behavior**:
   - When the trigger is held, a new sample is selected and played at a
     configurable interval (e.g., every 500 ms).
   - The angle points to specific "item types" distributed around 360 degrees.
   - The angle selection allows for randomness: the closer the stick is to a
     defined angle, the heavily weighted it is to pick that item type.
3. **Assets**:
   - Found in the `assets/` directory.
   - Follow a prefix and variation schema (e.g.
     `ui-pickup-<type>-<variation>.wav` or `<type>-<variation>.wav`).

## Current Implementation State

- **PC Prototype (`proto.py`)**: Built using Python and `Pygame`.
- Scans `assets/` directory dynamically to categorize item variations.
- Includes math to map categories cleanly around a 360-degree radius.
- Includes angular distance math to select sounds probabilistically.
- Includes a fallback to use the mouse instead of a physical joystick if needed.

## Future Plans (Hardware Target)

Once the angle math, layout, and "feel" are verified in the Python prototype,
the logic will be ported to C++ for the Teensy microcontroller.

- **Framework**: PlatformIO.
- **Hardware**: Teensy (with audio shield natively, or raw DAC).
- **Libraries**: `Bounce2` for trigger debouncing, `Teensy Audio Library` for
  mixing/playing multiple sound variations concurrently from an SD card or flash
  memory.

## Setup & Running on Mac (Prototype)

1. Ensure Python 3 is installed.
2. Sync the project dependencies using `uv`:
   ```bash
   uv sync
   # Or simply run without manual setup: uv run proto.py
   ```
3. Run the prototype:
   ```bash
   source .venv/bin/activate
   python proto.py
   ```

## Teensy Wiring (SD Reader + Joystick)

These wiring instructions match the current C++ firmware pin mapping used in
`include/config.h` and `src/main.cpp`.

### 1) SPI SD-card Reader Module -> Teensy 4.0

Use the pin labels from your SD reader PCB (`CS`, `SCK`, `MOSI`, `MISO`, `VCC`,
`GND`) and connect them as follows:

| SD Reader Pin | Teensy 4.0 Pin | Notes                             |
| ------------- | -------------- | --------------------------------- |
| CS            | D10            | Chip select                       |
| SCK           | D13            | SPI clock                         |
| MOSI          | D11            | SPI data from Teensy to SD reader |
| MISO          | D12            | SPI data from SD reader to Teensy |
| VCC           | 3.3V           | Use 3.3V power                    |
| GND           | GND            | Common ground                     |

Important:

- Do not power the SD reader from 5V unless your specific module explicitly
  supports level shifting for 3.3V logic.
- Keep SPI wires reasonably short to avoid signal integrity issues.

### 2) KY-023 Joystick -> Teensy 4.0

Use the joystick pins in this order: `GND`, `+5V`, `VRx`, `VRy`, `SW`.

| Joystick Pin | Teensy 4.0 Pin | Notes                                         |
| ------------ | -------------- | --------------------------------------------- |
| GND          | GND            | Common ground                                 |
| +5V          | 3.3V           | Power from 3.3V, not 5V                       |
| VRx          | A2 (pin 16)    | X-axis analog input                           |
| VRy          | A3 (pin 17)    | Y-axis analog input                           |
| SW           | D2             | Button input (active-low with `INPUT_PULLUP`) |

Important:

- Even if the joystick board says `+5V`, use Teensy 3.3V so analog outputs stay
  in safe ADC range.
- Do not use A0/pin 14 for joystick analog input in this project, because A0 is
  reserved for DAC audio output in the analog-audio path.

## Firmware Variants (Teensy 4.0)

The project supports three hardware firmware variants for different iteration
stages:

| Variant | PlatformIO env | Asset source | Audio output |
| --- | --- | --- | --- |
| Most local iteration | `teensy40_local_usb` | Internal LittleFS (limited set) | USB Audio |
| SD asset iteration | `teensy40_sd_usb` | SD card (`.raw` files at card root) | USB Audio |
| Final embedded path | `teensy40_final_embedded` | SD card (`.raw` files at card root) | Teensy DAC (A0/pin 14) -> LM386 |

### A) Most local iteration (no SD card required)

1. Prepare limited hardware-test asset set:
  ```bash
  ./scripts/prepare_assets_hw_test.sh
  ```
2. Flash MTP transfer firmware and copy files into LittleFS:
  ```bash
  pio run -e teensy40_hwtest --target upload
  ```
3. Flash local USB-audio iteration variant:
  ```bash
  pio run -e teensy40_local_usb --target upload
  ```

### B) SD asset iteration (full SD library + USB audio)

1. Ensure SD card has `.raw` files in its root directory.
2. Flash SD + USB audio variant:
  ```bash
  pio run -e teensy40_sd_usb --target upload
  ```

### C) Final embedded path (SD + analog DAC output)

1. Ensure SD card has `.raw` files in its root directory.
2. Flash final analog-output variant:
  ```bash
  pio run -e teensy40_final_embedded --target upload
  ```

Backward compatibility notes:
- `teensy40` remains the production SD + analog output firmware.
- `teensy40_hwtest_usbaudio` remains available for the original LittleFS USB test flow.

### Convenience flash scripts

Instead of typing full PlatformIO commands each time:

```bash
./scripts/flash_local_usb.sh
./scripts/flash_sd_usb.sh
./scripts/flash_final_embedded.sh
```
