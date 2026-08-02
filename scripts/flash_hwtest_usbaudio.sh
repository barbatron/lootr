#!/usr/bin/env bash
# Flash the USB Audio firmware onto the Teensy.
# Run this AFTER flash_hwtest_mtp.sh + copying .raw files via MTP.
# Files on LittleFS survive this reflash.
#
# After flashing:
#   macOS System Settings → Sound → Output → "Teensy MIDI/Audio"
#   Push joystick + hold button → audio plays through Mac speakers/headphones
#
# Usage: ./scripts/flash_hwtest_usbaudio.sh
#
# Tip: if the upload hangs, press the small white button on the Teensy board.

set -euo pipefail

cd "$(dirname "$0")/.."

source .venv/bin/activate 2>/dev/null || true

echo "Flashing USB Audio firmware..."
pio run -e teensy40_hwtest_usbaudio --target upload
echo ""
echo "Next: System Settings → Sound → Output → 'Teensy MIDI/Audio'"
echo "Then: push joystick + hold button to trigger sounds"
