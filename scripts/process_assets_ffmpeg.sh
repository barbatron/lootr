#!/usr/bin/env bash
# Batch-trim silence from source WAV assets and emit processed WAV + RAW PCM.
#
# Usage:
#   ./scripts/process_assets_ffmpeg.sh
#   ./scripts/process_assets_ffmpeg.sh --input assets --output .generated/processed-assets
#   ./scripts/process_assets_ffmpeg.sh --sample-rate 22050

set -euo pipefail

cd "$(dirname "$0")/.."

INPUT_DIR="assets"
OUTPUT_ROOT=".generated/processed-assets"
SAMPLE_RATE=44100

while [[ $# -gt 0 ]]; do
  case "$1" in
    --input)
      INPUT_DIR="${2:-}"
      shift 2
      ;;
    --output)
      OUTPUT_ROOT="${2:-}"
      shift 2
      ;;
    --sample-rate)
      SAMPLE_RATE="${2:-}"
      shift 2
      ;;
    -h|--help)
      sed -n '1,14p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "Error: ffmpeg not found in PATH." >&2
  exit 1
fi

if [[ ! "$SAMPLE_RATE" =~ ^[0-9]+$ ]]; then
  echo "Error: --sample-rate must be an integer, got '$SAMPLE_RATE'" >&2
  exit 2
fi

if [[ ! -d "$INPUT_DIR" ]]; then
  echo "Error: input directory not found: $INPUT_DIR" >&2
  exit 1
fi

WAV_OUT="$OUTPUT_ROOT/wav"
RAW_OUT="$OUTPUT_ROOT/raw"
mkdir -p "$WAV_OUT" "$RAW_OUT"

FILTER="silenceremove=start_periods=1:start_duration=0.02:start_threshold=-45dB:stop_periods=-1:stop_duration=0.05:stop_threshold=-45dB"

processed=0
failed=0

sum_wav_bytes=0
sum_out_wav_bytes=0
sum_out_raw_bytes=0

human_bytes() {
  awk -v b="$1" 'BEGIN {
    split("B KiB MiB GiB TiB", u, " ");
    i = 1;
    while (b >= 1024 && i < 5) { b /= 1024; i++; }
    printf("%.2f %s", b, u[i]);
  }'
}

while IFS= read -r -d '' src; do
  base="$(basename "$src")"
  stem="${base%.*}"
  wav_dst="$WAV_OUT/${stem}.wav"
  raw_dst="$RAW_OUT/${stem}.raw"
  src_bytes=$(wc -c < "$src" | tr -d ' ')
  sum_wav_bytes=$((sum_wav_bytes + src_bytes))

  if ffmpeg -hide_banner -loglevel error -y \
      -i "$src" \
      -af "$FILTER" \
      -ar "$SAMPLE_RATE" \
      -ac 1 \
      -sample_fmt s16 \
      "$wav_dst"; then
    :
  else
    echo "FAIL (wav): $src" >&2
    failed=$((failed + 1))
    continue
  fi

  if ffmpeg -hide_banner -loglevel error -y \
      -i "$wav_dst" \
      -f s16le \
      -acodec pcm_s16le \
      -ac 1 \
      -ar "$SAMPLE_RATE" \
      "$raw_dst"; then
    wav_bytes=$(wc -c < "$wav_dst" | tr -d ' ')
    raw_bytes=$(wc -c < "$raw_dst" | tr -d ' ')
    sum_out_wav_bytes=$((sum_out_wav_bytes + wav_bytes))
    sum_out_raw_bytes=$((sum_out_raw_bytes + raw_bytes))
    processed=$((processed + 1))
    continue
  fi

  echo "FAIL (raw): $src" >&2
  failed=$((failed + 1))
done < <(find "$INPUT_DIR" -type f \( -iname "*.wav" \) -print0 | sort -z)

echo "Processed: $processed"
echo "Failed:    $failed"
echo "WAV out:   $WAV_OUT"
echo "RAW out:   $RAW_OUT"
echo "Input WAV total:    $sum_wav_bytes bytes ($(human_bytes "$sum_wav_bytes"))"
echo "Output WAV total:   $sum_out_wav_bytes bytes ($(human_bytes "$sum_out_wav_bytes"))"
echo "Output RAW total:   $sum_out_raw_bytes bytes ($(human_bytes "$sum_out_raw_bytes"))"

combined_out_bytes=$((sum_out_wav_bytes + sum_out_raw_bytes))
if [[ "$sum_wav_bytes" -gt 0 ]]; then
  change_pct=$(awk -v before="$sum_wav_bytes" -v after="$combined_out_bytes" 'BEGIN { printf("%.1f", ((after - before) / before) * 100.0) }')
  echo "Combined out vs input: ${change_pct}%"
fi

if [[ "$processed" -eq 0 ]]; then
  echo "Warning: no WAV files processed. Check --input path." >&2
fi
