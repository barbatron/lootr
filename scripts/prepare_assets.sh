#!/usr/bin/env bash
# prepare_assets.sh — Convert WAV assets to SPI flash-ready RAW files
#
# Usage:
#   ./scripts/prepare_assets.sh [assets_dir] [output_dir]
#
# Defaults:
#   assets_dir  = ./assets
#   output_dir  = ./assets_raw
#
# Requirements:
#   sox (brew install sox)
#
# Output format: 16-bit signed PCM, 44100 Hz, mono (.raw)
# This matches the format expected by AudioPlaySerialflashRaw on Teensy.
# After running this script, upload the .raw files to the SPI flash chip
# using the SerialFlash CopyFromSerial sketch (see spec.md).

set -euo pipefail

ASSETS_DIR="${1:-assets}"
OUTPUT_DIR="${2:-assets_raw}"

# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

if ! command -v sox &>/dev/null; then
    echo "Error: 'sox' is not installed. Install it with: brew install sox"
    exit 1
fi

if [ ! -d "$ASSETS_DIR" ]; then
    echo "Error: assets directory '$ASSETS_DIR' not found."
    exit 1
fi

WAV_FILES=("$ASSETS_DIR"/*.wav)
if [ ! -e "${WAV_FILES[0]}" ]; then
    echo "No .wav files found in '$ASSETS_DIR'. Nothing to do."
    exit 0
fi

mkdir -p "$OUTPUT_DIR"

# ---------------------------------------------------------------------------
# Conversion
# ---------------------------------------------------------------------------

echo "Converting WAV → RAW (44100 Hz, mono, 16-bit signed PCM)"
echo "  Source : $ASSETS_DIR"
echo "  Output : $OUTPUT_DIR"
echo ""

converted=0
skipped=0

for wav in "$ASSETS_DIR"/*.wav; do
    filename=$(basename "$wav" .wav)
    out="$OUTPUT_DIR/${filename}.raw"

    # Skip if output is already newer than source
    if [ -f "$out" ] && [ "$out" -nt "$wav" ]; then
        echo "  [skip]  $filename.raw (up to date)"
        ((skipped++)) || true
        continue
    fi

    echo "  [conv]  $filename.wav → $filename.raw"
    sox "$wav" \
        --rate 44100 \
        --channels 1 \
        --encoding signed-integer \
        --bits 16 \
        "$out"
    ((converted++)) || true
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo ""
echo "Done. $converted converted, $skipped skipped."
echo ""
echo "Next step: upload all .raw files in '$OUTPUT_DIR/' to the SPI flash chip."
echo "Use the SerialFlash 'CopyFromSerial' example sketch — see spec.md for details."
