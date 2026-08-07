#!/usr/bin/env bash
# Flash SD-backed firmware with USB audio output.
#
# This variant reads .raw assets from SD card root and outputs audio over USB
# (useful for Windows/Mac iteration without onboard amplifier/speaker).
#
# Usage: ./scripts/flash_sd_usb.sh

set -euo pipefail

cd "$(dirname "$0")/.."

source .venv/bin/activate 2>/dev/null || true

echo "Flashing SD-backed USB-audio firmware..."
pio run -e teensy40_sd_usb --target upload

echo ""
echo "Next: ensure OS output/listen path is set for Teensy USB audio."
