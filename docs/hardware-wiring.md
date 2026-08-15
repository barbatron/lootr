# Hardware Wiring Guide (Pico 2020 Primary)

## Pico + SD Reader (SPI0)

Use GPIO names and verify physical pin numbers:

| SD Reader Pin | Pico GPIO   | Pico Physical Pin   |
| ------------- | ----------- | ------------------- |
| CS            | GP17        | 22                  |
| SCK           | GP18        | 24                  |
| MOSI / DI     | GP19        | 25                  |
| MISO / DO     | GP16        | 21                  |
| GND           | GND         | 23                  |
| VCC           | 3V3 or VBUS | 36 (3V3), 40 (VBUS) |

Notes:

- Start with 3V3 VCC.
- If module requires higher input, test VBUS only when signal levels are safe.
- Keep wires short and mechanically stable.

## Pico + KY-023 Joystick

| KY-023 Pin | Pico GPIO   | Pico Physical Pin |
| ---------- | ----------- | ----------------- |
| GND        | GND         | 23                |
| +5V label  | 3V3         | 36                |
| VRx        | GP26 (ADC0) | 31                |
| VRy        | GP27 (ADC1) | 32                |
| SW         | GP15        | 20                |

Notes:

- Power joystick from 3.3V.
- SW is active low when pressed.

## Shared Ground Requirement

All modules must share the same ground reference:

- Pico GND
- SD module GND
- Joystick GND
- Audio amplifier GND

## Build Hygiene Checklist

- Firm connector seating
- No crossed DI/DO lines
- No loose breadboard rails
- USB cable known-good for data
