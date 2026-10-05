#!/usr/bin/env bash
# Resolve the newest available PlatformIO CLI binary.
#
# Usage:
#   ./scripts/find_pio.sh
#   ./scripts/find_pio.sh --verbose

set -euo pipefail

VERBOSE=0
if [[ "${1:-}" == "--verbose" ]]; then
  VERBOSE=1
fi

collect_candidates() {
  command -v -a pio 2>/dev/null || true
  if [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
    echo "$HOME/.platformio/penv/bin/pio"
  fi
}

best_path=""
best_version=""

while IFS= read -r candidate; do
  [[ -n "$candidate" ]] || continue
  [[ -x "$candidate" ]] || continue

  line="$("$candidate" --version 2>/dev/null | head -n 1 || true)"
  version="$(printf '%s\n' "$line" | sed -n 's/.*version \([0-9][0-9.]*\).*/\1/p')"
  [[ -n "$version" ]] || continue

  if [[ "$VERBOSE" -eq 1 ]]; then
    echo "candidate: $candidate ($version)" >&2
  fi

  if [[ -z "$best_path" ]]; then
    best_path="$candidate"
    best_version="$version"
    continue
  fi

  if [[ "$(printf '%s\n%s\n' "$best_version" "$version" | sort -V | tail -n 1)" == "$version" ]]; then
    if [[ "$version" != "$best_version" || "$candidate" != "$best_path" ]]; then
      best_path="$candidate"
      best_version="$version"
    fi
  fi
done < <(collect_candidates | awk '!seen[$0]++')

if [[ -z "$best_path" ]]; then
  echo "Error: no usable PlatformIO CLI found in PATH or ~/.platformio/penv/bin/pio." >&2
  exit 1
fi

if [[ "$VERBOSE" -eq 1 ]]; then
  echo "selected: $best_path ($best_version)" >&2
fi

echo "$best_path"
