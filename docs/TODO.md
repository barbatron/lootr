# Project TODOs

This list tracks handheld build work from SD and joystick diagnostics to
standalone audio playback.

## Milestone A - Stable Inputs and Storage

- [x] Raspberry Pi Pico SD diagnostics passes with stable file counts
- [x] Raspberry Pi Pico joystick serial diagnostics confirmed
- [ ] Combined diagnostic firmware: SD + joystick in one sketch

## Milestone B - Audio Bring-Up

- [ ] Select audio path for initial hardware integration: LM386 or class-D
      module
- [ ] Generate known test tone from Pico firmware
- [ ] Confirm audible output on tiny speaker
- [ ] Tune gain and filter network to remove noise/clipping

## Milestone C - Power and Enclosure

- [ ] Select power source (USB power bank first)
- [ ] Add inline latching power switch on 5V rail
- [ ] Verify boot reliability from switched power
- [ ] Add enclosure layout with access to USB, SD, and switch

## Milestone D - Integrated Behavior

- [ ] Load and play SD assets from trigger input
- [ ] Add simple runtime status logs over serial
- [ ] Validate 10-minute continuous run on battery power
- [ ] Validate handling stability (no random resets or SD dropouts)

## Stretch Goals

- [ ] Add battery level indication
- [ ] Add startup self-test status LEDs
- [ ] Add volume control knob
