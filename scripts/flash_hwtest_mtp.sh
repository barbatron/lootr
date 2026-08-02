#!/usr/bin/env bash
# Flash the MTP file-transfer firmware onto the Teensy.
# After flashing, "Lootr Test" appears as a USB drive in Finder.
# Drag assets_test_raw/*.raw onto it, then run flash_hwtest_usbaudio.sh.
#
# Usage: ./scripts/flash_hwtest_mtp.sh
#
# Tip: if the upload hangs, press the small white button on the Teensy board
# to put it into programming mode.

set -euo pipefail

cd "$(dirname "$0")/.."

source .venv/bin/activate 2>/dev/null || true

echo "Flashing MTP (file transfer) firmware..."
pio run -e teensy40_hwtest --target upload
