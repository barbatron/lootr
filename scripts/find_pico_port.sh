#!/usr/bin/env bash
# Prints the most likely serial device for Raspberry Pi Pico on macOS.
#
# Why port names change:
# - macOS assigns /dev/cu.usbmodemNNN identifiers dynamically.
# - NNN can change after unplug/replug, reboot, or BOOTSEL reset.
#
# Usage:
#   ./scripts/find_pico_port.sh
#   ./scripts/find_pico_port.sh --all

set -euo pipefail

if [[ "${1:-}" == "--all" ]]; then
  ls -1 /dev/cu.usbmodem* /dev/tty.usbmodem* 2>/dev/null || true
  exit 0
fi

pick_port() {
  local candidates=()
  while IFS= read -r line; do
    [[ -n "$line" ]] && candidates+=("$line")
  done < <(ls -t /dev/cu.usbmodem* /dev/tty.usbmodem* 2>/dev/null || true)

  if [[ ${#candidates[@]} -eq 0 ]]; then
    return 1
  fi

  printf '%s\n' "${candidates[0]}"
}

if ! port="$(pick_port)"; then
  echo "Error: no Pico-style usbmodem serial port found." >&2
  echo "Tip: connect Pico in normal run mode (not BOOTSEL mass-storage mode)." >&2
  exit 1
fi

echo "$port"
