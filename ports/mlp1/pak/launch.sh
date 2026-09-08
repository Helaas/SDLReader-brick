#!/bin/sh
# SDL Reader launcher for Miniloong Pocket 1 (MLP1) / Leaf launcher.
# No first-run docs.pdf logic; no docs.pdf dependency.

set -eu

# Resolve SD card root using Leaf's pattern
find_sdcard_root() {
    for d in /mnt/sdcard /mnt/SDCARD /storage /sdcard; do
        [ -d "$d" ] && echo "$d" && return 0
    done
    echo "/mnt/sdcard"
}

SDCARD_ROOT=$(find_sdcard_root)

# Source Leaf runtime paths if available
if [ -f "${SDCARD_ROOT}/Apps/env.sh" ]; then
    . "${SDCARD_ROOT}/Apps/env.sh"
fi

PAK_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PAK_BASENAME=$(basename -- "$PAK_DIR")
PAK_NAME=${PAK_BASENAME%.*}
BIN_DIR="$PAK_DIR/bin"

cd "$PAK_DIR"

# State directory
SHARED_USERDATA_ROOT=${SHARED_USERDATA_PATH:-"${HOME:-/tmp}/.userdata"}
export HOME="$SHARED_USERDATA_ROOT/$PAK_NAME"
mkdir -p "$HOME"

# Logging
LOG_ROOT=${LOGS_PATH:-"$SHARED_USERDATA_ROOT/logs"}
mkdir -p "$LOG_ROOT"
LOG_FILE="$LOG_ROOT/$PAK_NAME.txt"
: >"$LOG_FILE"
exec >>"$LOG_FILE"
exec 2>&1

echo "=== Launching $PAK_NAME (MLP1) at $(date) ==="
echo "Arguments: $*"
echo "SD card root: $SDCARD_ROOT"

# Wayland SDL backend (MLP1 uses Weston compositor)
if [ -d "/var/run" ]; then
    export SDL_VIDEODRIVER=wayland
fi

# Leaf-aware defaults
export SDL_READER_DEFAULT_DIR="${SDL_READER_DEFAULT_DIR:-$SDCARD_ROOT}"
export SDL_READER_STATE_DIR="${SDL_READER_STATE_DIR:-${UMRK_APPS_DATA_PATH:-$HOME}}"

# Library path
if [ -d "$PAK_DIR/lib" ]; then
    if [ -n "${LD_LIBRARY_PATH-}" ]; then
        export LD_LIBRARY_PATH="$PAK_DIR/lib:$LD_LIBRARY_PATH"
    else
        export LD_LIBRARY_PATH="$PAK_DIR/lib"
    fi
fi

READER_BIN="$BIN_DIR/sdl_reader_cli"

if [ ! -x "$READER_BIN" ]; then
    echo "ERROR: Reader binary not found or not executable: $READER_BIN" >&2
    exit 1
fi

echo "=== Starting file browser ==="
exec "$READER_BIN" -b "$@"
