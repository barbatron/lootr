#!/usr/bin/env bash
# Build and upload the Pico integration firmware with PlatformIO.
#
# Usage:
#   ./scripts/pio_upload.sh
#   ./scripts/pio_upload.sh --asset-sampling 0.70

set -euo pipefail

cd "$(dirname "$0")/.."

ASSET_SAMPLING_OVERRIDE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --asset-sampling)
      ASSET_SAMPLING_OVERRIDE="${2:-}"
      shift 2
      ;;
    -h|--help)
      sed -n '1,7p' "$0"
      exit 0
      ;;
    *)
      echo "Usage: $0 [--asset-sampling 0.0..1.0]" >&2
      exit 2
      ;;
  esac
done

if [[ -n "$ASSET_SAMPLING_OVERRIDE" ]]; then
  if ! awk -v v="$ASSET_SAMPLING_OVERRIDE" 'BEGIN{ exit (v+0==v && v>=0.0 && v<=1.0) ? 0 : 1 }'; then
    echo "Error: --asset-sampling must be within 0.0..1.0, got '$ASSET_SAMPLING_OVERRIDE'." >&2
    exit 2
  fi
fi

if ! PIO_BIN="$(./scripts/find_pio.sh)"; then
  echo "Error: PlatformIO CLI not found." >&2
  exit 1
fi

cmd=("$PIO_BIN" run -e pico_lootr_integration --target upload)
if [[ -n "$ASSET_SAMPLING_OVERRIDE" ]]; then
  echo "Uploading with ASSET_SAMPLING=$ASSET_SAMPLING_OVERRIDE"
  PLATFORMIO_BUILD_FLAGS="-DASSET_SAMPLING=$ASSET_SAMPLING_OVERRIDE" "${cmd[@]}"
  exit 0
fi

"${cmd[@]}"
