#!/usr/bin/env bash
# Strong debug: show every command; fail on any error/undefined var; keep pipefail.
[ -n "$BASH_VERSION" ] || { echo "Please run with bash (not sh)."; exit 2; }
set -Eeuo pipefail
trap 'echo "ERROR at line $LINENO"; exit 1' ERR
set -x

# --- Config ---
BIN="${BIN:-./bin/sdl_reader_cli}"
DEST="${DEST:-./bundle}"
BINDIR="$DEST/bin"
LIBDIR="$DEST/lib"

# Exclude:
# - glibc core (must use device versions)
# - system dynamic loader
# - system SDL2 (use device's SDL2 which has the right KMSDRM/fbcon backends)
# If the device also provides SDL2_ttf, you can optionally add ^libSDL2_ttf-2\.0\.so\. here too.
EXCL_REGEX='(^ld-linux-|^libc\.so\.|^libpthread\.so\.|^libm\.so\.|^librt\.so\.|^libdl\.so\.|^libnsl\.so\.|^libresolv\.so\.|^libSDL2-2\.0\.so\.)'

echo "START bundling from: $PWD"
echo "Binary: $BIN"
echo "Dest:   $DEST"

# Sanity: binary must exist
[ -f "$BIN" ] || { echo "Missing binary: $BIN"; exit 1; }

# Fresh dirs
mkdir -p "$BINDIR" "$LIBDIR"

# Copy binary
cp -f "$BIN" "$BINDIR/"

copy_one() {
  local src="$1"
  local base
  base="$(basename "$src")"

  # skip excluded
  if echo "$base" | grep -Eq "$EXCL_REGEX"; then
    echo "Skip (excluded): $base"
    return 0
  fi

  # copy the soname/symlink itself
  if [ ! -e "$LIBDIR/$base" ]; then
    cp -u "$src" "$LIBDIR/" || true
    echo "Copied: $base"
  fi

  # if it’s a symlink, also copy the real file it points to
  if [ -L "$src" ]; then
    local tgt
    tgt="$(readlink -f "$src")"
    local tgtbase
    tgtbase="$(basename "$tgt")"
    if [ ! -e "$LIBDIR/$tgtbase" ]; then
      cp -u "$tgt" "$LIBDIR/" || true
      echo "Copied target: $tgtbase"
    fi
  fi
}

# Pass 1: deps of the binary
ldd "$BIN" | awk '
  /=>/ && $3 ~ /^\// { print $3 }
  /^[[:space:]]*\/.*\.so/ { print $1 }
' | sort -u | while read -r lib; do
  [ -f "$lib" ] && copy_one "$lib"
done

# Pass 2..N: pull transitive deps
for _ in 1 2 3; do
  find "$LIBDIR" -maxdepth 1 -type f -name '*.so*' | while read -r so; do
    ldd "$so" 2>/dev/null | awk '
      /=>/ && $3 ~ /^\// { print $3 }
      /^[[:space:]]*\/.*\.so/ { print $1 }
    ' | while read -r dep; do
      [ -f "$dep" ] && copy_one "$dep"
    done
  done
done

# Ensure SONAME symlinks exist
for real in "$LIBDIR"/*.so*; do
  [ -L "$real" ] && continue
  soname="$(LANG=C readelf -d "$real" 2>/dev/null | awk -F'[][]' '/SONAME/{print $2}' || true)"
  if [ -n "${soname:-}" ] && [ ! -e "$LIBDIR/$soname" ]; then
    ln -s "$(basename "$real")" "$LIBDIR/$soname"
    echo "Linked SONAME: $soname -> $(basename "$real")"
  fi
done

# RPATH/RUNPATH patching:
# - Remove any existing RPATH
# - Set RUNPATH on the binary to $ORIGIN/../lib (patchelf writes DT_RUNPATH by default)
# - Set RUNPATH on each bundled .so to $ORIGIN (siblings can find each other)
if command -v patchelf >/dev/null 2>&1; then
  patchelf --remove-rpath "$BINDIR/sdl_reader_cli" 2>/dev/null || true
  patchelf --set-rpath '$ORIGIN/../lib' "$BINDIR/sdl_reader_cli"
  for so in "$LIBDIR"/*.so*; do
    patchelf --remove-rpath "$so" 2>/dev/null || true
    patchelf --set-rpath '$ORIGIN' "$so" || true
  done
else
  echo "WARNING: patchelf not found; skipping RPATH/RUNPATH embed."
fi

# Launcher with env setup and video fallback logic
cat > "$BINDIR/run.sh" <<'EOF'
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
EOF
chmod +x "$BINDIR/run.sh"

# Optional: prune heavy libs you likely don't need on the Brick (uncomment if desired)
 rm -f "$LIBDIR"/libpulse*.so* "$LIBDIR"/libsystemd*.so* "$LIBDIR"/libdbus-1*.so* || true
 rm -f "$LIBDIR"/libX*.so* "$LIBDIR"/libSM*.so* "$LIBDIR"/libICE*.so* "$LIBDIR"/libwayland-*.so* || true

# Size trim (optional)
strip "$BINDIR/sdl_reader_cli" 2>/dev/null || true
strip --strip-unneeded "$LIBDIR"/*.so* 2>/dev/null || true

echo "DONE. Bundle ready: $DEST"
ls -la "$DEST" "$BINDIR" "$LIBDIR" || true
