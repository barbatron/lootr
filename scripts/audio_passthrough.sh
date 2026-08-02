#!/usr/bin/env bash
# Route Teensy USB audio to Mac speakers/headphones via ffplay.
#
# The Teensy sends audio TO the Mac over USB (it appears as a Mac input device).
# macOS won't pass this through to speakers automatically — this script does it.
#
# Usage: ./scripts/audio_passthrough.sh
# Stop:  Ctrl+C

set -euo pipefail

DEVICE="Teensy MIDI_Audio"

command -v ffplay >/dev/null 2>&1 || {
    echo "ERROR: ffplay not found. Install with: brew install ffmpeg"
    exit 1
}

# Verify the device is present
if ! ffmpeg -f avfoundation -list_devices true -i "" 2>&1 | grep -q "$DEVICE"; then
    echo "ERROR: '$DEVICE' not found. Is the Teensy plugged in and running usbaudio firmware?"
    ffmpeg -f avfoundation -list_devices true -i "" 2>&1 | grep -A1 "AVFoundation audio"
    exit 1
fi

echo "Routing '$DEVICE' → Mac speakers"
echo "Press Ctrl+C to stop."
echo ""

exec ffplay -f avfoundation -i "none:${DEVICE}" -nodisp -loglevel quiet
