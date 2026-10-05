#!/usr/bin/env bash
# Quick diagnostics for the Pico PlatformIO workflow.
#
# Usage:
#   ./scripts/pio_doctor.sh
#   ./scripts/pio_doctor.sh --build   # also run a full compile check

set -euo pipefail

cd "$(dirname "$0")/.."

RUN_BUILD=0
if [[ "${1:-}" == "--build" ]]; then
  RUN_BUILD=1
fi

echo "== Lootr PlatformIO Doctor =="

if ! PIO_BIN="$(./scripts/find_pio.sh)"; then
  echo "FAIL: PlatformIO CLI not found." >&2
  exit 1
fi

echo "PlatformIO binary: $PIO_BIN"
"$PIO_BIN" --version

echo ""
echo "-- Checking project config --"
"$PIO_BIN" project config >/dev/null
echo "PASS: platformio.ini parses successfully"

echo ""
echo "-- Checking env exists --"
PROJECT_CONFIG="$("$PIO_BIN" project config)"
if printf '%s\n' "$PROJECT_CONFIG" | grep -Fq "env:pico_lootr_integration"; then
  echo "PASS: env:pico_lootr_integration found"
else
  echo "FAIL: env:pico_lootr_integration not found in project config" >&2
  exit 1
fi

echo ""
echo "-- Checking Pico serial port detection --"
if PORT="$(./scripts/find_pico_port.sh)"; then
  echo "PASS: detected serial port $PORT"
else
  echo "WARN: no Pico usbmodem port found right now"
fi

echo "All detected usbmodem ports:"
./scripts/find_pico_port.sh --all || true

if [[ "$RUN_BUILD" -eq 1 ]]; then
  echo ""
  echo "-- Running compile check --"
  "$PIO_BIN" run -e pico_lootr_integration
fi

echo ""
echo "Doctor complete."
