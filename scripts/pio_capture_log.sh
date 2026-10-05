#!/usr/bin/env bash
# Upload firmware, capture monitor output, compact repeated lines, and print log path.
#
# Usage:
#   ./scripts/pio_capture_log.sh

set -euo pipefail

cd "$(dirname "$0")/.."

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  sed -n '1,6p' "$0"
  exit 0
fi

if [[ "$#" -ne 0 ]]; then
  echo "Usage: $0" >&2
  exit 2
fi

mkdir -p .temp

next_fix_index() {
  local max=0
  local f base n
  for f in .temp/monitor_after_fix*.log; do
    [[ -e "$f" ]] || continue
    base="$(basename "$f")"
    if [[ "$base" =~ ^monitor_after_fix([0-9]+)(_compact)?\.log$ ]]; then
      n="${BASH_REMATCH[1]}"
      if (( n > max )); then
        max="$n"
      fi
    fi
  done
  echo $((max + 1))
}

idx="$(next_fix_index)"
raw_log=".temp/monitor_after_fix${idx}.log"
compact_log=".temp/monitor_after_fix${idx}_compact.log"

echo "Using raw log:      $raw_log"
echo "Compacted log path: $compact_log"
echo ""
echo "Uploading firmware..."
ASSET_SAMPLING=${ASSET_SAMPLING:-1}
./scripts/pio_upload.sh --asset-sampling "$ASSET_SAMPLING"

echo ""
echo "Waiting 1s before monitor..."
sleep 1

if ! command -v script >/dev/null 2>&1; then
  echo "Error: 'script' command not found; cannot capture interactive monitor output." >&2
  exit 1
fi

if ! PIO_BIN="$(./scripts/find_pio.sh)"; then
  echo "Error: PlatformIO CLI not found." >&2
  exit 1
fi

if ! PORT="$(./scripts/find_pico_port.sh)"; then
  echo "Error: no Pico serial port found." >&2
  exit 1
fi

echo "Starting monitor on $PORT. Press Ctrl+C when done capturing."
echo ""

set +e
script -q "$raw_log" "$PIO_BIN" device monitor --port "$PORT" --baud 115200
monitor_status=$?
set -e

if [[ ! -s "$raw_log" ]]; then
  echo "Error: monitor log is empty: $raw_log" >&2
  exit 1
fi

raw_clean="$(mktemp "${TMPDIR:-/tmp}/lootr-monitor-clean.XXXXXX.log")"
cleanup() {
  rm -f "$raw_clean"
}
trap cleanup EXIT

sed -e '/^Script started on /d' -e '/^Script done on /d' "$raw_log" > "$raw_clean"
./scripts/compact_log_repeats.sh "$raw_clean" > "$compact_log"
rm -f "$raw_log"

if [[ "$monitor_status" -ne 0 && "$monitor_status" -ne 130 ]]; then
  echo "Warning: monitor exited with status $monitor_status" >&2
fi

echo ""
echo "Compacted log written to:"
echo "$compact_log"
