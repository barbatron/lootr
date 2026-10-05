#!/usr/bin/env bash
# Collapse consecutive identical log lines.
#
# Usage:
#   ./scripts/compact_log_repeats.sh path/to/log.txt
#   cat path/to/log.txt | ./scripts/compact_log_repeats.sh

set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  sed -n '1,7p' "$0"
  exit 0
fi

if [[ "$#" -gt 1 ]]; then
  echo "Usage: $0 [log-file]" >&2
  exit 2
fi

awk_script='
function flush_run() {
  if (!have_line) return
  print prev_line
  if (run_count > 1) {
    printf("   (repeated x%d times)\n", run_count)
  }
}
{
  if (!have_line) {
    prev_line = $0
    run_count = 1
    have_line = 1
    next
  }

  if ($0 == prev_line) {
    run_count++
    next
  }

  flush_run()
  prev_line = $0
  run_count = 1
}
END {
  flush_run()
}
'

if [[ "$#" -eq 1 ]]; then
  awk "$awk_script" "$1"
else
  awk "$awk_script"
fi
