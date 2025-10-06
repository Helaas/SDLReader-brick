#!/bin/sh
# SDL Reader launcher with built-in ImGui file browser
# - On first run: automatically opens docs.pdf
# - After first run: launches with internal file browser (-b option)
# - The ImGui browser stores last directory in config.json (lastBrowseDirectory field)
#   and defaults to /mnt/SDCARD

set -u

cd "$(dirname "$0")"
export LD_LIBRARY_PATH="$(pwd)/lib:${LD_LIBRARY_PATH:-}"

FIRST_RUN_FLAG="./.first_run_done"
LOG="/tmp/sdl_reader.log"

# Clear the log at startup
: > "$LOG"

# First run: open docs.pdf if not done before
if [ ! -f "$FIRST_RUN_FLAG" ] && [ -f "./res/docs.pdf" ]; then
  echo "=== First run at $(date) ===" >> "$LOG"
  echo "Opening docs.pdf" >> "$LOG"
  ./bin/sdl_reader_cli "./res/docs.pdf" 2>&1 | tee -a "$LOG"
  touch "$FIRST_RUN_FLAG"
  echo "First run completed" >> "$LOG"
fi

# Launch with internal file browser
echo "=== SDL Reader started at $(date) ===" >> "$LOG"
echo "Launching with internal file browser (-b)" >> "$LOG"
./bin/sdl_reader_cli -b 2>&1 | tee -a "$LOG"
