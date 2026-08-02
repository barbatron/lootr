#!/usr/bin/env bash
# Prepare a minimal hardware-test asset set for LittleFS / internal Teensy flash.
#
# Picks PICKS_PER_CAT files per angle direction from the full asset library,
# converts them to 16-bit signed PCM mono .raw with ffmpeg.
#
# Usage:
#   ./scripts/prepare_assets_hw_test.sh [options] [assets_dir [output_dir]]
#
# Options:
#   --rate HZ        Sample rate in Hz (default: 44100)
#                    Use 22050 to halve file sizes if flash is tight.
#                    NOTE: if you change this, set AUDIO_SAMPLE_RATE_EXACT
#                    accordingly in main_hw_test.cpp before flashing.
#   --picks N        Files to pick per category (default: 2)
#
# Requires: ffmpeg

set -euo pipefail

RATE=44100
PICKS=2
ASSETS_DIR="assets"
OUT_DIR="assets_test_raw"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rate)  RATE="$2";  shift 2 ;;
        --picks) PICKS="$2"; shift 2 ;;
        *)
            ASSETS_DIR="$1"; shift
            [[ $# -gt 0 ]] && OUT_DIR="$1" && shift
            ;;
    esac
done

command -v ffmpeg >/dev/null 2>&1 || { echo "ERROR: ffmpeg not found. Install with: brew install ffmpeg"; exit 1; }

mkdir -p "$OUT_DIR"

# ── Category definitions (align with ITEM_ANGLE_RULES in config.h) ─────────
# Format: "cat_name:direction_label:space-separated keywords"
# Keywords are tried in order; the first one with matching files wins.

CATEGORIES=(
    "metal:270° up:metal can gun pipe"
    "ore:180° left:charcol stone sulfur"
    "wood:0° right:wood plank stick log"
    "cloth:transfer layer:cloth"
)

# ── Helpers ─────────────────────────────────────────────────────────────────

convert_wav() {
    local src="$1" dst="$2"
    ffmpeg -y -loglevel error -i "$src" -ar "$RATE" -ac 1 -f s16le "$dst"
}

file_kb() { echo $(( $(wc -c < "$1") / 1024 )); }

# ── Main loop ───────────────────────────────────────────────────────────────

echo "Hardware test asset prep"
echo "  Rate:   ${RATE} Hz  (44100 = full quality, 22050 = half size)"
echo "  Picks:  ${PICKS} per category"
echo "  Input:  ${ASSETS_DIR}/"
echo "  Output: ${OUT_DIR}/"
echo ""

total_bytes=0
total_files=0

for entry in "${CATEGORIES[@]}"; do
    IFS=':' read -r cat_name direction keywords <<< "$entry"
    echo "[${cat_name} → ${direction}]"
    found=0
    for kw in $keywords; do
        [[ $found -ge $PICKS ]] && break
        while IFS= read -r wav; do
            [[ $found -ge $PICKS ]] && break
            base=$(basename "$wav" .wav)
            out="${OUT_DIR}/${base}.raw"
            if convert_wav "$wav" "$out"; then
                kb=$(file_kb "$out")
                total_bytes=$(( total_bytes + kb * 1024 ))
                total_files=$(( total_files + 1 ))
                found=$(( found + 1 ))
                echo "  + ${base}.raw  (${kb} KB)"
            fi
        done < <(find "$ASSETS_DIR" -maxdepth 1 -name "ui-pickup-${kw}*.wav" | sort)
    done
    [[ $found -eq 0 ]] && echo "  (no files matched keywords: ${keywords})"
done

echo ""
echo "Done: ${total_files} files, $(( total_bytes / 1024 )) KB in ${OUT_DIR}/"
echo ""
if [[ $RATE -ne 44100 ]]; then
    echo "  !! Sample rate is ${RATE} Hz — update main_hw_test.cpp:"
    echo "     #define AUDIO_SAMPLE_RATE_EXACT ${RATE}.0f"
    echo ""
fi
echo "Next steps:"
echo "  1. Flash hw_test firmware:  pio run -e teensy40_hwtest --target upload"
echo "  2. Teensy mounts as USB drive 'Lootr Test' in Finder"
echo "  3. Drag all *.raw files from ${OUT_DIR}/ onto the drive"
echo "  4. Eject, press Teensy reset button — joystick + audio ready"
