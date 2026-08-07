#!/usr/bin/env bash
# Flash final embedded firmware: SD-backed assets + onboard analog DAC output.
#
# Output path for this variant is Teensy DAC (A0/pin 14) -> LM386 -> speaker.
#
# Usage: ./scripts/flash_final_embedded.sh

set -euo pipefail

cd "$(dirname "$0")/.."

source .venv/bin/activate 2>/dev/null || true

echo "Flashing final embedded firmware (SD assets + analog DAC output)..."
pio run -e teensy40_final_embedded --target upload

echo ""
echo "Next: verify SD card is inserted and analog audio path is wired."
