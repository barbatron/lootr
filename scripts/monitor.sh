#!/usr/bin/env bash
# Open the serial monitor for the Teensy hardware test firmware.
# Shows startup messages, discovered file count, and live angle/trigger readout.
#
# Usage: ./scripts/monitor.sh [env]
#   env defaults to teensy40_hwtest_usbaudio
#   Use 'teensy40_hwtest' when monitoring the MTP firmware instead.
#
# Press Ctrl+C to exit.

set -euo pipefail

cd "$(dirname "$0")/.."

source .venv/bin/activate 2>/dev/null || true

ENV="${1:-teensy40_hwtest_usbaudio}"

echo "Opening serial monitor for environment: $ENV"
echo "Press Ctrl+C to exit."
echo ""
pio device monitor -e "$ENV"
