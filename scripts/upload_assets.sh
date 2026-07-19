#!/usr/bin/env bash
# upload_assets.sh — Upload .raw audio assets to SPI flash chip via Teensy
#
# Usage:
#   ./scripts/upload_assets.sh [assets_raw_dir]
#
# Default:
#   assets_raw_dir = ./assets_raw
#
# What this script does:
#   1. Builds and flashes the SerialFlash CopyFromSerial sketch to the Teensy
#   2. Waits for the Teensy to re-enumerate on USB
#   3. Runs the CopyFromSerial Python uploader to send all .raw files
#   4. Builds and flashes the main Lootr firmware back
#
# Requirements:
#   - PlatformIO CLI  (pip install platformio)
#   - Python 3
#   - Teensy connected via USB

set -euo pipefail

ASSETS_RAW_DIR="${1:-assets_raw}"
MAIN_SRC="src/main.cpp"
COPY_SKETCH_SRC=".pio/libdeps/teensy40/SerialFlash/examples/CopyFromSerial/CopyFromSerial.ino"
COPY_UPLOADER=".pio/libdeps/teensy40/SerialFlash/examples/CopyFromSerial/CopyFromSerial.py"
BACKUP_SRC="src/main.cpp.bak"

# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

if ! command -v pio &>/dev/null; then
    echo "Error: 'pio' not found. Install with: pip install platformio"
    exit 1
fi

if ! command -v python3 &>/dev/null; then
    echo "Error: 'python3' not found."
    exit 1
fi

if [ ! -d "$ASSETS_RAW_DIR" ]; then
    echo "Error: assets_raw directory '$ASSETS_RAW_DIR' not found."
    echo "Run ./scripts/prepare_assets.sh first."
    exit 1
fi

RAW_FILES=("$ASSETS_RAW_DIR"/*.raw)
if [ ! -e "${RAW_FILES[0]}" ]; then
    echo "No .raw files found in '$ASSETS_RAW_DIR'. Nothing to upload."
    exit 0
fi

# Ensure lib deps are installed (needed for the sketch and uploader paths)
echo "==> Ensuring PlatformIO lib deps are installed..."
pio pkg install

if [ ! -f "$COPY_SKETCH_SRC" ]; then
    echo "Error: CopyFromSerial sketch not found at: $COPY_SKETCH_SRC"
    echo "Run 'pio pkg install' and try again."
    exit 1
fi

if [ ! -f "$COPY_UPLOADER" ]; then
    echo "Error: CopyFromSerial.py uploader not found at: $COPY_UPLOADER"
    exit 1
fi

# ---------------------------------------------------------------------------
# Detect serial port
# ---------------------------------------------------------------------------

detect_port() {
    # Try common macOS Teensy port patterns
    for pattern in /dev/cu.usbmodem* /dev/tty.usbmodem*; do
        ports=($pattern)
        if [ -e "${ports[0]}" ]; then
            echo "${ports[0]}"
            return 0
        fi
    done
    echo ""
}

PORT=$(detect_port)
if [ -z "$PORT" ]; then
    echo "Error: No Teensy USB serial port detected. Is it connected?"
    exit 1
fi
echo "==> Detected Teensy on: $PORT"

# ---------------------------------------------------------------------------
# Step 1 — Swap in CopyFromSerial sketch and flash
# ---------------------------------------------------------------------------

echo ""
echo "==> [1/4] Backing up main firmware source..."
cp "$MAIN_SRC" "$BACKUP_SRC"

echo "==> [2/4] Flashing CopyFromSerial sketch to Teensy..."
cp "$COPY_SKETCH_SRC" "$MAIN_SRC"

# Ensure we restore main.cpp on exit, even on error
restore_main() {
    if [ -f "$BACKUP_SRC" ]; then
        echo ""
        echo "==> Restoring main firmware source..."
        cp "$BACKUP_SRC" "$MAIN_SRC"
        rm "$BACKUP_SRC"
    fi
}
trap restore_main EXIT

pio run --target upload --environment teensy40

echo "==> Waiting for Teensy to re-enumerate..."
sleep 3

# Re-detect port after re-enumeration
PORT=$(detect_port)
if [ -z "$PORT" ]; then
    echo "Error: Teensy port not found after reflash. Try pressing the Teensy button."
    exit 1
fi
echo "==> Teensy re-enumerated on: $PORT"

# ---------------------------------------------------------------------------
# Step 2 — Upload .raw files via CopyFromSerial Python script
# ---------------------------------------------------------------------------

echo ""
echo "==> [3/4] Uploading ${#RAW_FILES[@]} .raw file(s) to SPI flash..."
echo ""

# Check filename length (SerialFlash limit: 20 chars including extension)
for f in "${RAW_FILES[@]}"; do
    name=$(basename "$f")
    if [ ${#name} -gt 20 ]; then
        echo "Warning: '$name' is ${#name} chars — SerialFlash limit is 20. Rename it."
    fi
done

python3 "$COPY_UPLOADER" "$PORT" "${RAW_FILES[@]}"

echo ""
echo "==> Asset upload complete."

# ---------------------------------------------------------------------------
# Step 3 — Restore and reflash main firmware
# ---------------------------------------------------------------------------

echo ""
echo "==> [4/4] Reflashing main Lootr firmware..."
restore_main  # copy backup back before building
trap - EXIT   # clear trap, restore already done

pio run --target upload --environment teensy40

echo ""
echo "All done!"
echo "Assets are on the SPI flash chip and main firmware is running."
echo "Monitor output with: pio device monitor"
