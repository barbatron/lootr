#!/usr/bin/env bash
# Flash local iteration firmware: internal LittleFS assets + USB audio output.
#
# This variant does not use the SD card for playback, but it expects assets to
# already be loaded into LittleFS (for example via flash_hwtest_mtp.sh).
#
# Usage: ./scripts/flash_local_usb.sh

set -euo pipefail

cd "$(dirname "$0")/.."

source .venv/bin/activate 2>/dev/null || true

echo "Flashing local USB-audio firmware (internal LittleFS assets)..."
pio run -e teensy40_local_usb --target upload

echo ""
echo "Next: set OS output to Teensy USB audio and test joystick trigger."
