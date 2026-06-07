#!/usr/bin/env bash
# ADB staging helper for MLP1 SDLReader.pak
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MLP1_PORT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$MLP1_PORT/../.." && pwd)"
PAK_SRC="$PROJECT_ROOT/build/mlp1/package/SDLReader.pak"
ADB="${ADB:-adb}"
SERIAL="${ADB_SERIAL:-}"

echo "=== MLP1 ADB Stage ==="

# Determine serial
if [ -z "$SERIAL" ]; then
    SERIAL=$($ADB devices | awk 'NR>1 && $2=="device" {print $1; exit}')
fi

if [ -z "$SERIAL" ]; then
    echo "Error: No online adb device found."
    exit 1
fi
ADB_CMD="$ADB -s $SERIAL"

# Ensure package is built
if [ ! -d "$PAK_SRC" ]; then
    echo "Package not found at $PAK_SRC"
    echo "Running package-mlp1 first..."
    cd "$PROJECT_ROOT" && make package-mlp1
fi

if [ ! -d "$PAK_SRC" ]; then
    echo "Error: Package still not found after build."
    exit 1
fi

echo "Pushing SDLReader.pak to device..."

# Push to MLP1 Leaf path
DEVICE_PATH="/mnt/sdcard/Apps/mlp1/SDLReader.pak"
$ADB_CMD shell "rm -rf '$DEVICE_PATH' && mkdir -p /mnt/sdcard/Apps/mlp1"
$ADB_CMD push "$PAK_SRC" "/mnt/sdcard/Apps/mlp1/"
$ADB_CMD shell "chmod +x '$DEVICE_PATH/launch.sh'"
$ADB_CMD shell "chmod +x '$DEVICE_PATH/bin/sdl_reader_cli'"

echo "=== Stage complete ==="
echo "SDCard path: $DEVICE_PATH"
