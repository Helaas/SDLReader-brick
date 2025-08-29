#!/bin/sh
# File browser using minui-list with nested JSON ("items")
# - Folders first, then PDFs (case-insensitive)
# - Uses custom confirm_text for folder vs file
# - Remembers last visited folder between runs
# - Selecting a PDF exec's ./bin/sdl_reader_cli <file.pdf>
# - DEBUG logs to /tmp/sdlreader_browser.log (set DEBUG=0 to disable)

###############################################################################
# Config & environment
###############################################################################
set -u

cd "$(dirname "$0")"
export LD_LIBRARY_PATH="$(pwd)/lib:${LD_LIBRARY_PATH:-}"

BASE="/mnt/SDCARD"
JSON_FILE="/tmp/sdlreader_browser.json"
SELECTION_FILE="/tmp/sdlreader_selection.txt"
STATE_PATH="/tmp/sdlreader_lastdir.txt"   # last folder persistence
LOG="/tmp/sdlreader_browser.log"
DEBUG="${DEBUG:0}"                    # set DEBUG=-1 for logs

###############################################################################
# Helpers
###############################################################################
log() {
  [ "$DEBUG" -eq 0 ] && return 0
  printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG"
}

dump_file() {
  [ "$DEBUG" -eq 0 ] && return 0
  file="$1"; label="$2"
  log "--- DUMP BEGIN: $label ($file) ---"
  if [ -s "$file" ]; then
    sed 's/[[:cntrl:]]/\?/g' "$file" >> "$LOG" 2>/dev/null || cat "$file" >> "$LOG"
  else
    echo "(empty or missing)" >> "$LOG"
  fi
  log "--- DUMP END: $label ---"
}

hex_of_str() {
  printf '%s' "$1" | od -An -tx1 -v 2>/dev/null | tr -s ' ' ' '
}

json_escape() {
  # JSON-escape a raw string, returning a quoted JSON string
  printf '%s' "$1" | ./bin/jq -Rr @json
}

in_base() {
  case "$1" in
    "$BASE"|"$BASE"/*) return 0 ;;
    *) return 1 ;;
  esac
}

save_lastdir() {
  d="$1"
  if in_base "$d"; then
    printf '%s' "$d" > "$STATE_PATH"
    log "Saved lastdir: $d"
  else
    log "Not saving outside BASE: $d"
  fi
}

###############################################################################
# Sanity checks
###############################################################################
[ -d "$BASE" ] || { echo "Base directory not found: $BASE" >&2; exit 1; }
[ -x ./bin/minui-list ] || { echo "./bin/minui-list not found or not executable" >&2; exit 1; }
[ -x ./bin/jq ] || { echo "./bin/jq not found or not executable" >&2; exit 1; }

# Fresh log per run
: > "$LOG"
log "Starting browser. BASE=$BASE"

###############################################################################
# Build JSON ("items") for minui-list
###############################################################################
build_json_list() {
  dir="$1"
  log "build_json_list: dir='$dir'"

  dirs_tmp="$(mktemp -t fbrowse_dirs.XXXXXX)"
  pdfs_tmp="$(mktemp -t fbrowse_pdfs.XXXXXX)"
  items_tmp="$(mktemp -t fbrowse_items.XXXXXX)"

  # Collect entries (skip hidden; '*' glob ignores dotfiles)
  for p in "$dir"/*; do
    [ -e "$p" ] || continue
    if [ -d "$p" ]; then
      printf '%s\n' "$p" >>"$dirs_tmp"
    elif [ -f "$p" ]; then
      case "$p" in
        *.[Pp][Dd][Ff]) printf '%s\n' "$p" >>"$pdfs_tmp" ;;
      esac
    fi
  done

  : >"$items_tmp"

  # Up item (virtual)
  if [ "$dir" != "$BASE" ]; then
    echo '{"name":"..","path":"__UP__","features":{"confirm_text":"Go up"}}' >>"$items_tmp"
  fi

  # Folders first (sorted, case-insensitive)
  if [ -s "$dirs_tmp" ]; then
    sort -f "$dirs_tmp" | while IFS= read -r p; do
      b="$(basename "$p")"
      name_json=$(json_escape "$b/")   # display label
      path_json=$(json_escape "$p")    # absolute path
      printf '{ "name": %s, "path": %s, "features": { "confirm_text": "Open folder" } }\n' \
        "$name_json" "$path_json" >>"$items_tmp"
    done
  fi

  # PDFs after (sorted, case-insensitive)
  if [ -s "$pdfs_tmp" ]; then
    sort -f "$pdfs_tmp" | while IFS= read -r p; do
      b="$(basename "$p")"
      name_json=$(json_escape "$b")
      path_json=$(json_escape "$p")
      printf '{ "name": %s, "path": %s, "features": { "confirm_text": "Open file" } }\n' \
        "$name_json" "$path_json" >>"$items_tmp"
    done
  fi

  # Wrap into {"items":[ ... ]}
  ./bin/jq -s '{items: .}' "$items_tmp" >"$JSON_FILE"

  rm -f "$dirs_tmp" "$pdfs_tmp" "$items_tmp"

  dump_file "$JSON_FILE" "LIST JSON for dir '$dir'"
}

###############################################################################
# Resolve selection -> absolute path
###############################################################################
resolve_path_from_name() {
  sel_name="$1"   # what minui-list printed
  json="$2"
  ./bin/jq -r --arg n "$sel_name" '
    .items[]
    | select(
        (.name == $n)
        or ((.name|rtrimstr("/")) == $n)
        or ((.name) == ($n + "/"))
      )
    | .path
  ' "$json" | head -n 1
}

###############################################################################
# Main browse loop
###############################################################################
browse() {
  dir="$1"

  while :; do
    build_json_list "$dir"

    log "Invoking minui-list in dir='$dir'"
    : > "$SELECTION_FILE"
    ./bin/minui-list \
      --file "$JSON_FILE" \
      --item-key "items" \
      --title "Browsing: $dir" \
      --write-location "$SELECTION_FILE"
    status=$?

    # Read back selection (first line), trim CR
    if [ -s "$SELECTION_FILE" ]; then
      selection="$(tr -d '\r' < "$SELECTION_FILE" | head -n1)"
    else
      selection=""
    fi

    dump_file "$SELECTION_FILE" "SELECTION_FILE"
    log "minui-list exit status: $status"
    log "Raw selection: '$selection'"
    log "Selection HEX : $(hex_of_str "$selection")"

    # User cancelled or nothing selected
    [ $status -ne 0 ] && { log "Non-zero status, exiting."; exit 0; }
    [ -z "$selection" ] && { log "Empty selection, exiting."; exit 0; }

    # Handle ".."
    if [ "$selection" = ".." ]; then
      if [ "$dir" != "$BASE" ]; then
        parent="$(dirname "$dir")"
        log "Going up: $dir -> $parent"
        dir="$parent"
        save_lastdir "$dir"
      else
        log "At BASE, ignoring '..'"
      fi
      continue
    fi

    # Map selection -> absolute path using our JSON
    sel_path="$(resolve_path_from_name "$selection" "$JSON_FILE")"
    log "Resolved sel_path via JSON: '$sel_path'"

    # Fallback #1: if empty and selection looks absolute, use as-is
    if [ -z "$sel_path" ] && [ "${selection#/}" != "$selection" ]; then
      sel_path="$selection"
      log "Fallback #1: using absolute-looking selection as path: '$sel_path'"
    fi

    # Fallback #2: construct from current dir (handles basename returns)
    if [ -z "$sel_path" ]; then
      sel_stripped="${selection%/}"
      candidate="$dir/$sel_stripped"
      if [ -e "$candidate" ]; then
        sel_path="$candidate"
        log "Fallback #2: constructed candidate path: '$sel_path'"
      fi
    fi

    # If still empty, rebuild
    if [ -z "$sel_path" ]; then
      log "ERROR: Could not resolve selection '$selection' to a path. Rebuilding."
      continue
    fi

    # Branch by type
    if [ -d "$sel_path" ]; then
      log "Navigating into directory: '$sel_path'"
      dir="$sel_path"
      save_lastdir "$dir"
      continue
    fi

    case "$sel_path" in
      *.[Pp][Dd][Ff])
        log "PDF selected. Executing sdl_reader_cli with: '$sel_path'"
        if [ ! -x ./bin/sdl_reader_cli ]; then
          echo "Error: ./bin/sdl_reader_cli not found or not executable" >&2
          exit 1
        fi
        # Optionally remember the current directory before launching
        save_lastdir "$dir"
        
        # Set up logging with rotation (keep last 5 runs)
        READER_LOG="/tmp/sdl_reader.log"
        if [ -f "$READER_LOG" ] && [ $(wc -l < "$READER_LOG") -gt 1000 ]; then
          # Rotate logs if getting too large
          [ -f "$READER_LOG.4" ] && rm -f "$READER_LOG.4"
          [ -f "$READER_LOG.3" ] && mv "$READER_LOG.3" "$READER_LOG.4"
          [ -f "$READER_LOG.2" ] && mv "$READER_LOG.2" "$READER_LOG.3"
          [ -f "$READER_LOG.1" ] && mv "$READER_LOG.1" "$READER_LOG.2"
          mv "$READER_LOG" "$READER_LOG.1"
        fi
        
        # Log with timestamp and file info
        echo "=== SDL Reader started at $(date) ===" >> "$READER_LOG"
        echo "File: $sel_path" >> "$READER_LOG"
        echo "Working directory: $(pwd)" >> "$READER_LOG"
        echo "--- Binary output follows ---" >> "$READER_LOG"
        
        exec ./bin/sdl_reader_cli "$sel_path" 2>&1 | tee -a "$READER_LOG"
        ;;
      *)
        log "Unknown selection type for path '$sel_path' (not dir, not .pdf). Rebuilding."
        ;;
    esac
  done
}

###############################################################################
# Determine starting directory (remember last)
###############################################################################
start_dir="$BASE"
if [ -r "$STATE_PATH" ]; then
  last="$(cat "$STATE_PATH")"
  if [ -n "$last" ] && [ -d "$last" ] && in_base "$last"; then
    start_dir="$last"
    log "Restoring lastdir: $start_dir"
  fi
fi

browse "$start_dir"
