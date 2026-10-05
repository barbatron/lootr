# Lootr — Hardware Specification & Porting Guide

This file is the source of truth for Lootr selection logic and hardware pin
expectations.

Primary hardware target is Raspberry Pi Pico 2020 (RP2040).

---

## Hardware Bill of Materials

| Component    | Part / Notes                                                                            |
| ------------ | --------------------------------------------------------------------------------------- |
| MCU          | Raspberry Pi Pico 2020 (RP2040)                                                         |
| Input        | KY-023 analog thumbstick module (X, Y potentiometers + push-button)                     |
| Storage      | MicroSD card reader (SPI interface)                                                     |
| Audio output | MAX98357A (I2S class-D amplifier)                                                     |
| Misc         | MicroSD card, short jumper wires, stable USB power                                      |

---

## Pin Mapping (Primary)

### MicroSD Card Reader -> Raspberry Pi Pico 2020

| SD Reader Pin | Pico GPIO   | Pico Physical Pin     |
| ------------- | ----------- | --------------------- |
| CS            | GP17        | 22                    |
| MOSI / DI     | GP19        | 25                    |
| MISO / DO     | GP16        | 21                    |
| SCK           | GP18        | 24                    |
| VCC           | 3V3 or VBUS | 36 (3V3) or 40 (VBUS) |
| GND           | GND         | 23                    |

Notes:

- Use GPIO labels (GP16..GP19), not only physical location descriptions.
- Start with 3V3 VCC. If init fails and module supports 5V input, test VBUS.
- Keep MISO/DO logic safe for RP2040 inputs.

### KY-023 Thumbstick -> Raspberry Pi Pico 2020

Mapping for diagnostics/prototyping:

| Thumbstick Pin | Pico Pin    | Notes                   |
| -------------- | ----------- | ----------------------- |
| VCC            | 3V3         |                         |
| GND            | GND         |                         |
| VRx            | GP26 (ADC0) | Joystick X axis         |
| VRy            | GP27 (ADC1) | Joystick Y axis         |
| SW             | GP15        | Active LOW with pull-up |

> ADC conversion note: scale to -1.0..1.0 from midpoint.

---

## Arduino IDE Core Requirement (Pico)

Use Earle Philhower RP2040 boards package for the current Pico SD diagnostic
sketch.

Board Manager URL:

```text
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

---

## Audio Asset Format

Files on the SD card should follow:

```text
<type>-<variation>.wav
```

- type: category keyword
- variation: zero-padded integer, e.g., 01, 02
- format: WAV supported by AudioTools `WAVDecoder` (44.1 kHz mono recommended)

---

## Item Type -> Angle Rules

Angle convention: 0 degrees = right, 90 = down, 180 = left, 270 = up.

| Category           | Keywords                                    | Angle |
| ------------------ | ------------------------------------------- | ----- |
| Metal / mechanical | metal, can, gun, pipe, blade, wire          | 270   |
| Minerals / earth   | charcoal, sulfur, sulphur, stone, ore, coal | 180   |
| Wood               | wood, plank, stick, log                     | 0     |
| Everything else    | fallback                                    | 90    |

---

## Selection Algorithm

### get_angular_distance(a, b)

Shortest angular distance between two angles (0..180).

```text
diff = abs((a - b) % 360)
return min(diff, 360 - diff)
```

### pick_item_for_angle(input_angle, max_spread)

1. For each item type, compute angular distance.
2. If distance <= spread, use weight = (spread - distance)^2.
3. Weighted-random choose from candidates.
4. If no candidates, choose closest item.

### Dynamic Spread (joystick amplitude)

```text
amplitude = sqrt(x^2 + y^2)
spread = SPREAD_AT_CENTER - clamp(amplitude, 0, 1) * (SPREAD_AT_CENTER - SPREAD_AT_EDGE)
```

---

## Key Constants

| Constant         | Value | Description                        |
| ---------------- | ----- | ---------------------------------- |
| PLAY_INTERVAL_MS | 115   | Minimum ms between sample triggers |
| DEADZONE         | 0.01  | Amplitude threshold                |
| SPREAD_AT_CENTER | 180.0 | Max spread at center               |
| SPREAD_AT_EDGE   | 15.0  | Min spread at edge                 |
