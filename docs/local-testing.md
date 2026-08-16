# Local Testing Guide

This guide covers repeatable local validation loops for Pico development.

## 1) Environment Check

- Connect Pico over USB
- Confirm Arduino IDE board package for Earle Philhower RP2040 is installed
- Confirm serial monitor baud rate is 115200

## 2) SD Reader Validation

Sketch:

- ../raspberry_pi_pico_sd_diag/raspberry_pi_pico_sd_diag.ino

Expected success output:

- CS=17 -> SD.begin OK, files=<non-zero>, raw=<non-zero>
- Repeated [PASS ...] lines

Expected failure output:

- No working CS pin found.
- Repeated [FAIL ...] no active CS

Useful runtime commands in serial monitor:

- r : force CS rescan
- s : print status counters

## 3) Joystick Validation (No Audio)

Sketch:

- ../raspberry_pi_pico_joystick_diag/raspberry_pi_pico_joystick_diag.ino

Expected stream:

- rawX=..., rawY=..., normX=..., normY=..., sw=released
- sw=PRESSED when button is held

Useful runtime commands:

- c : recalibrate center (leave stick untouched)
- h : print help

## 4) Failure Triage

If upload fails with "No drive to deploy":

- Close serial monitor
- Enter BOOTSEL mode and replug USB
- Confirm RPI-RP2 drive appears before upload

If SD fails but serial and joystick work:

- Re-check DI/DO mapping
- Re-seat SD module and jumper wires
- Test alternate SD reader module

If joystick values are stuck:

- Verify joystick VCC is 3.3V, not 5V
- Verify VRx/VRy are on ADC pins (GP26/GP27)

## 5) Integrated Playback Validation

Sketch:

- ../raspberry_pi_pico_lootr_integration/raspberry_pi_pico_lootr_integration.ino

Expected behavior:

- Boot log prints `Ready.` and asset count
- Hold joystick button and move stick to trigger short `.raw` playback snippets
- Serial prints `play:` lines with angle, amplitude, spread, and mapped target
  angle

Commands:

- c : joystick recalibration
- r : SD asset rescan
- s : status
- p : play first asset snippet

If no sound but serial `play:` appears:

- Confirm MAX98357A Vin is powered (VBUS/5V preferred)
- Confirm SD pin on amp is held HIGH (or connected to GP13 for this sketch)
- Confirm speaker is only on amp + / - pads and not tied to GND
