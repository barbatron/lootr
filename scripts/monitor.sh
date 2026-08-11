#!/usr/bin/env bash
# Open a serial monitor for firmware output without using PlatformIO monitor.
#
# Usage:
#   ./scripts/monitor.sh [env]
#   ./scripts/monitor.sh [env] [seconds]
#
# Examples:
#   ./scripts/monitor.sh teensy40_sd_diag        # interactive monitor (Ctrl+C)
#   ./scripts/monitor.sh teensy40_sd_diag 5      # quick 5s smoke test
#
# In quick-test mode (seconds provided), the script exits after the duration and
# returns:
#   0 if at least one [PASS ...] line was seen
#   1 if any [FAIL ...] line was seen or no PASS line was seen

set -euo pipefail

cd "$(dirname "$0")/.."

source .venv/bin/activate 2>/dev/null || true

ENV="${1:-teensy40_hwtest_usbaudio}"
SECONDS_LIMIT="${2:-}"

detect_port() {
	local patterns=(
		"/dev/cu.usbmodem*"
		"/dev/tty.usbmodem*"
		"/dev/cu.usbserial*"
		"/dev/tty.usbserial*"
		"/dev/cu.SLAB_USBtoUART*"
		"/dev/tty.SLAB_USBtoUART*"
	)

	for pattern in "${patterns[@]}"; do
		for p in $pattern; do
			if [[ -e "$p" ]]; then
				echo "$p"
				return 0
			fi
		done
	done

	echo ""
}

if [[ -n "$SECONDS_LIMIT" ]] && ! [[ "$SECONDS_LIMIT" =~ ^[0-9]+$ ]]; then
	echo "Error: seconds must be an integer, got '$SECONDS_LIMIT'"
	exit 2
fi

echo "Opening serial monitor for environment: $ENV"

if ! command -v python3 >/dev/null 2>&1; then
	echo "Error: python3 not found"
	exit 1
fi

PORT="$(detect_port)"
if [[ -z "$PORT" ]]; then
	echo "Error: no serial device found (usbmodem/usbserial)."
	exit 1
fi

echo "Using serial port: $PORT"

if [[ -z "$SECONDS_LIMIT" ]]; then
	echo "Press Ctrl+C to exit."
	echo ""
	python3 -m serial.tools.miniterm "$PORT" 115200
	exit 0
fi

echo "Quick-test mode: capture for ${SECONDS_LIMIT}s"
echo ""

LOG_FILE="$(mktemp "${TMPDIR:-/tmp}/lootr-monitor.XXXXXX.log")"
cleanup() {
	rm -f "$LOG_FILE"
}
trap cleanup EXIT

set +e
python3 -m serial.tools.miniterm "$PORT" 115200 >"$LOG_FILE" 2>&1 &
MON_PID=$!
set -e

sleep "$SECONDS_LIMIT"

if kill -0 "$MON_PID" 2>/dev/null; then
	kill "$MON_PID" 2>/dev/null || true
	wait "$MON_PID" 2>/dev/null || true
fi

cat "$LOG_FILE"

if grep -q "\[PASS" "$LOG_FILE"; then
	if grep -q "\[FAIL" "$LOG_FILE"; then
		echo ""
		echo "Quick-test result: UNSTABLE (both PASS and FAIL seen)"
		exit 1
	fi
	echo ""
	echo "Quick-test result: PASS"
	exit 0
fi

if grep -q "\[FAIL" "$LOG_FILE"; then
	echo ""
	echo "Quick-test result: FAIL"
	exit 1
fi

echo ""
echo "Quick-test result: NO PASS/FAIL LINES CAPTURED"
exit 1
