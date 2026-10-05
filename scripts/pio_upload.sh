#!/usr/bin/env bash
# Build and upload the Pico integration firmware with PlatformIO.
#
# Usage:
#   ./scripts/pio_upload.sh

set -euo pipefail

cd "$(dirname "$0")/.."

find_pio() {
  if command -v pio >/dev/null 2>&1; then
    command -v pio
    return 0
  fi
  if [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
    echo "$HOME/.platformio/penv/bin/pio"
    return 0
  fi
  return 1
}

if ! PIO_BIN="$(find_pio)"; then
  echo "Error: PlatformIO CLI not found (tried PATH and ~/.platformio/penv/bin/pio)." >&2
  exit 1
fi

"$PIO_BIN" run -e pico_lootr_integration --target upload
