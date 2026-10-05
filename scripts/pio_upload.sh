#!/usr/bin/env bash
# Build and upload the Pico integration firmware with PlatformIO.
#
# Usage:
#   ./scripts/pio_upload.sh

set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v pio >/dev/null 2>&1; then
  echo "Error: pio not found in PATH" >&2
  exit 1
fi

pio run -e pico_lootr_integration --target upload
