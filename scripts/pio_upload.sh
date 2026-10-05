#!/usr/bin/env bash
# Build and upload the Pico integration firmware with PlatformIO.
#
# Usage:
#   ./scripts/pio_upload.sh

set -euo pipefail

cd "$(dirname "$0")/.."

if ! PIO_BIN="$(./scripts/find_pio.sh)"; then
  echo "Error: PlatformIO CLI not found." >&2
  exit 1
fi

"$PIO_BIN" run -e pico_lootr_integration --target upload
