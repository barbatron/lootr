# Lootr Audio Synth / Sampler Project

## Project Overview
An interactive audio application that plays categorized sound samples based on joystick input. The project maps joystick X/Y coordinates to an angle and amplitude to dynamically select and play different material/item sound effects.

Ultimately, this project is intended to be flashed onto hardware (such as a Teensy 4.0/4.1 using PlatformIO). To iterate on the audio selection math and the UX quickly, there is currently a Python prototype.

## Core Mechanics
1. **Inputs**: 
   - X/Y Joystick: Angle determines the item category, amplitude determines volume/intensity.
   - Play Trigger: A button that enables playback.
2. **Playback behavior**: 
   - When the trigger is held, a new sample is selected and played at a configurable interval (e.g., every 500 ms).
   - The angle points to specific "item types" distributed around 360 degrees.
   - The angle selection allows for randomness: the closer the stick is to a defined angle, the heavily weighted it is to pick that item type.
3. **Assets**: 
   - Found in the `assets/` directory.
   - Follow a prefix and variation schema (e.g. `ui-pickup-<type>-<variation>.wav` or `<type>-<variation>.wav`).

## Current Implementation State
- **PC Prototype (`proto.py`)**: Built using Python and `Pygame`. 
- Scans `assets/` directory dynamically to categorize item variations.
- Includes math to map categories cleanly around a 360-degree radius.
- Includes angular distance math to select sounds probabilistically.
- Includes a fallback to use the mouse instead of a physical joystick if needed.

## Future Plans (Hardware Target)
Once the angle math, layout, and "feel" are verified in the Python prototype, the logic will be ported to C++ for the Teensy microcontroller.
- **Framework**: PlatformIO.
- **Hardware**: Teensy (with audio shield natively, or raw DAC).
- **Libraries**: `Bounce2` for trigger debouncing, `Teensy Audio Library` for mixing/playing multiple sound variations concurrently from an SD card or flash memory.

## Setup & Running on Mac (Prototype)

1. Ensure Python 3 is installed.
2. Set up a virtual environment and install Pygame with `uv`:
   ```bash
   uv venv
   source .venv/bin/activate
   uv pip install pygame
   # Or simply: uv run python proto.py
   ```
3. Run the prototype:
   ```bash
   python proto.py
   ```
