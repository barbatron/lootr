#!/usr/bin/env bash
# Open a serial monitor for Pico firmware output with PlatformIO.
#
# Usage:
#   ./scripts/monitor.sh [seconds]
#
# Examples:
#   ./scripts/monitor.sh                         # interactive monitor (Ctrl+C)
#   ./scripts/monitor.sh 5                       # quick 5s smoke test
#
# In quick-test mode (seconds provided), the script exits after the duration and
# returns:
#   0 if at least one [PASS ...] line was seen
#   1 if any [FAIL ...] line was seen or no PASS line was seen

set -euo pipefail

cd "$(dirname "$0")/.."

SECONDS_LIMIT="${1:-}"

if [[ -n "$SECONDS_LIMIT" ]] && ! [[ "$SECONDS_LIMIT" =~ ^[0-9]+$ ]]; then
	echo "Error: seconds must be an integer, got '$SECONDS_LIMIT'"
	exit 2
fi

if ! PIO_BIN="$(./scripts/find_pio.sh)"; then
	echo "Error: PlatformIO CLI not found."
	exit 1
fi

PORT="$(./scripts/find_pico_port.sh)"
echo "Opening monitor on: $PORT"

if [[ -z "$SECONDS_LIMIT" ]]; then
	echo "Press Ctrl+C to exit."
	echo ""
	"$PIO_BIN" device monitor --port "$PORT" --baud 115200
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
"$PIO_BIN" device monitor --port "$PORT" --baud 115200 >"$LOG_FILE" 2>&1 &
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
