#!/usr/bin/env bash
# Flash SD diagnostics firmware (no audio required).
#
# This firmware scans likely CS pins and continuously verifies SD init +
# root directory read every 500ms to expose flaky jumper connections.
#
# Usage: ./scripts/flash_sd_diag.sh

set -euo pipefail

cd "$(dirname "$0")/.."

source .venv/bin/activate 2>/dev/null || true

echo "Flashing SD diagnostics firmware..."
pio run -e teensy40_sd_diag --target upload

echo ""
echo "Next: ./scripts/monitor.sh teensy40_sd_diag"
echo "Expected: repeated [PASS ...] lines if wiring is stable."
