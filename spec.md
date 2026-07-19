# Lootr — Hardware Specification & Porting Guide

This file, together with `proto.py`, forms the complete source of truth for the
Loot-o-mo-tron. A language model given both files should have everything needed
to generate or update the Teensy 4.0 C++ sketch.

---

## Hardware Bill of Materials

| Component    | Part / Notes                                                              |
| ------------ | ------------------------------------------------------------------------- |
| MCU          | Teensy 4.0                                                                |
| Input        | KY-023 analog thumbstick module (X, Y potentiometers + push-button)       |
| Storage      | SPI NOR flash, e.g. Winbond W25Q128 (16 MB, 3.3V, SPI)                    |
| Audio output | Teensy DAC (pin 14 / A14) → LM386 audio amp → 8Ω speaker                  |
| Power        | 3.7V LiPo → MT3608 (or similar) boost converter → 5V rail                 |
| Misc         | 10kΩ volume potentiometer, 250µF output cap, 10µF + 10Ω stability network |

---

## Pin Mapping

### KY-023 Thumbstick → Teensy 4.0

| Thumbstick Pin | Teensy Pin  | Notes                                        |
| -------------- | ----------- | -------------------------------------------- |
| VCC            | 3.3V        |                                              |
| GND            | GND         |                                              |
| VRx            | A0 (pin 14) | Joystick X axis                              |
| VRy            | A1 (pin 15) | Joystick Y axis                              |
| SW             | D2          | Trigger button; use INPUT_PULLUP, active LOW |

> ADC reads 0–1023. Midpoint ≈ 512. Normalise to -1.0–1.0:
> `float x = (analogRead(A0) - 512) / 512.0f;`

### SPI Flash → Teensy 4.0

| Flash Pin | Teensy Pin |
| --------- | ---------- |
| CS        | D10        |
| MOSI      | D11        |
| MISO      | D12        |
| SCK       | D13        |
| VCC       | 3.3V       |
| GND       | GND        |

Recommended library: **SerialFlash** (by PJRC) or **Adafruit SPIFlash +
LittleFS**.

### Audio Output

|                   |                                                                               |
| ----------------- | ----------------------------------------------------------------------------- |
| Teensy DAC        | Pin A14 (pin 14 — also used for VRx; **route VRx to A1 instead if conflict**) |
| DAC → LM386 in+   | Via 10kΩ volume pot wiper                                                     |
| LM386 pin 6 (Vcc) | 5V rail from boost converter                                                  |
| LM386 pin 4 (GND) | GND                                                                           |
| LM386 pin 5 (out) | 250µF cap → speaker+                                                          |
| Speaker−          | GND                                                                           |
| Pin 7 stability   | 10µF + 10Ω in series, to GND                                                  |

> **Note**: If using the Teensy Audio Library with I2S output instead of raw
> DAC, connect an I2S DAC breakout (e.g. PCM5102) to Teensy pins 7 (LRCLK), 8
> (BCLK), 22 (MCLK), 13 (DIN). This gives significantly better audio quality.

---

## Audio Asset Format

Files stored on SPI flash should follow this naming convention:

```
<type>-<variation>.raw
```

- **type**: item category keyword (see angle rules below)
- **variation**: zero-padded integer, e.g. `01`, `02`
- **Format**: 16-bit PCM, 44100 Hz, mono (required by SerialFlash /
  AudioPlaySerialflashRaw)

Convert WAV → RAW with:

```bash
sox input.wav -r 44100 -c 1 -e signed -b 16 output.raw
```

---

## Item Type → Angle Rules

Angle convention: **0° = right, 90° = down, 180° = left, 270° = up**.

| Category           | Keywords                                    | Angle       |
| ------------------ | ------------------------------------------- | ----------- |
| Metal / mechanical | metal, can, gun, pipe, blade, wire          | 270° (up)   |
| Minerals / earth   | charcoal, sulfur, sulphur, stone, ore, coal | 180° (left) |
| Wood               | wood, plank, stick, log                     | 0° (right)  |
| Everything else    | _(fallback)_                                | 90° (down)  |

On hardware, implement as a static lookup in `include/config.h`.

---

## Selection Algorithm (port from `proto.py`)

### `get_angular_distance(a, b)`

Shortest angular distance between two angles (0–180°).

```
diff = abs((a - b) % 360)
return min(diff, 360 - diff)
```

### `pick_item_for_angle(input_angle, max_spread)`

1. For each item type, compute
   `dist = get_angular_distance(input_angle, type_angle)`.
2. If `dist <= max_spread`, compute `weight = (max_spread - dist)²`.
3. Randomly select using weighted probability.
4. If no candidates in spread, pick the closest item (fallback).

### Dynamic Spread (driven by joystick amplitude)

```
amplitude = sqrt(x² + y²)   // 0.0 at center, ~1.0 at edge
spread = SPREAD_AT_CENTER - clamp(amplitude, 0, 1) * (SPREAD_AT_CENTER - SPREAD_AT_EDGE)
```

---

## Key Constants (keep in sync with `proto.py` and `include/config.h`)

| Constant           | Value | Description                                           |
| ------------------ | ----- | ----------------------------------------------------- |
| `PLAY_INTERVAL_MS` | 180   | Minimum ms between sample triggers                    |
| `DEADZONE`         | 0.05  | Amplitude below which no trigger fires                |
| `SPREAD_AT_CENTER` | 180.0 | Max spread (degrees) at stick center → fully random   |
| `SPREAD_AT_EDGE`   | 20.0  | Min spread (degrees) at full stick deflection → tight |

---

## C++ Libraries Required (PlatformIO)

```ini
lib_deps =
    SerialFlash          ; SPI flash access + AudioPlaySerialflashRaw
    Bounce2              ; Trigger button debouncing
    ; Teensy Audio Library is bundled with Teensyduino — no separate entry needed
```

---

## Porting Checklist

- [ ] `get_angular_distance()` → port to
      `float getAngularDistance(float a, float b)`
- [ ] `pick_item_for_angle()` → port to
      `const char* pickItemForAngle(float angle, float spread)`
- [ ] `ITEM_ANGLE_RULES` → static array of structs in `config.h`
- [ ] `map_types_to_angles()` → not needed; asset list is static at build time
- [ ] `PLAY_INTERVAL_MS`, `DEADZONE`, `SPREAD_*` → `#define` in `config.h`
- [ ] ADC normalisation: `(analogRead(pin) - 512) / 512.0f`
- [ ] Trigger: `digitalRead(SW_PIN) == LOW` (active-low with pull-up)
- [ ] Audio playback: `AudioPlaySerialflashRaw` + `AudioMixer4` +
      `AudioOutputAnalog`
