#!/bin/sh
# SDL Reader launcher with built-in Nuklear file browser
#  - On first run (per shared userdata) opens docs.pdf.
#  - Subsequent runs launch the internal file browser (-b).

set -eu

# Resolve paths relative to this script
PAK_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PAK_BASENAME=$(basename -- "$PAK_DIR")
PAK_NAME=${PAK_BASENAME%.*}
BIN_DIR="$PAK_DIR/bin"

# Ensure we always run from the pak directory
cd "$PAK_DIR"

# Shared userdata home for this pak (fallback to $HOME/.userdata if not provided)
SHARED_USERDATA_ROOT=${SHARED_USERDATA_PATH:-"$HOME/.userdata"}
export HOME="$SHARED_USERDATA_ROOT/$PAK_NAME"
mkdir -p "$HOME"

# Resolve logging location (fallback to shared userdata logs directory)
LOG_ROOT=${LOGS_PATH:-"$SHARED_USERDATA_ROOT/logs"}
mkdir -p "$LOG_ROOT"
LOG_FILE="$LOG_ROOT/$PAK_NAME.txt"
: >"$LOG_FILE"

# Redirect stdout/stderr to log
exec >>"$LOG_FILE"
exec 2>&1

echo "=== Launching $PAK_NAME at $(date) ==="
echo "Arguments: $*"

# Build PATH entries for platform / architecture specific binaries if they exist
PATH_PREFIX=""
maybe_append_path() {
    [ -d "$1" ] || return 0
    if [ -z "$PATH_PREFIX" ]; then
        PATH_PREFIX="$1"
    else
        PATH_PREFIX="$PATH_PREFIX:$1"
    fi
}

maybe_append_path "$BIN_DIR/${PLATFORM:-}"
maybe_append_path "$BIN_DIR/${architecture:-}"
maybe_append_path "$BIN_DIR/shared"

if [ -n "$PATH_PREFIX" ]; then
    export PATH="$PATH_PREFIX:$PATH"
fi

# Configure default library and state directories for the reader
if [ -z "${SDL_READER_DEFAULT_DIR-}" ]; then
    export SDL_READER_DEFAULT_DIR="/mnt/SDCARD"
fi
export SDL_READER_STATE_DIR="$HOME"

# Library path (only prepend if lib directory exists)
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

FIRST_RUN_FLAG="$HOME/.first_run_done"
DOCS_PDF="$PAK_DIR/res/docs.pdf"

# First run: open docs.pdf if it exists
if [ ! -f "$FIRST_RUN_FLAG" ] && [ -f "$DOCS_PDF" ]; then
    echo "=== First run detected; opening docs.pdf ==="
    if "$READER_BIN" "$DOCS_PDF"; then
        touch "$FIRST_RUN_FLAG"
        echo "First run completed successfully."
    else
        echo "Warning: Initial docs.pdf launch failed." >&2
    fi
fi

echo "=== SDL Reader started at $(date) ==="
echo "Launching with internal file browser (-b)"
exec "$READER_BIN" -b "$@"
