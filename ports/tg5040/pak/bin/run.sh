#!/usr/bin/env sh
set -eu

HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export LD_LIBRARY_PATH="$HERE/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# XDG runtime (needed for KMSDRM/libinput on many systems)
: "${XDG_RUNTIME_DIR:=/tmp/runtime-$(id -u)}"
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

# Try KMSDRM if /dev/dri exists, else fbcon if /dev/fb0 exists, otherwise let SDL choose.
if [ -e /dev/dri/card0 ]; then
  export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
elif [ -e /dev/fb0 ]; then
  export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-fbcon}"
  export SDL_FBDEV="${SDL_FBDEV:-/dev/fb0}"
fi

# Sensible audio default
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"

# First attempt
if "$HERE/sdl_reader_cli" "$@"; then
  exit 0
fi

# Fallback to fbcon if kmsdrm fails
if [ "${SDL_VIDEODRIVER:-}" = "kmsdrm" ]; then
  echo "KMSDRM failed; retrying with fbcon..."
  export SDL_VIDEODRIVER=fbcon
  [ -e /dev/fb0 ] && export SDL_FBDEV=/dev/fb0
  exec "$HERE/sdl_reader_cli" "$@"
fi

exit 1
