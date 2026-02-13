#!/usr/bin/env bash
# Strong debug: show every command; fail on any error/undefined var; keep pipefail.
[ -n "$BASH_VERSION" ] || { echo "Please run with bash (not sh)."; exit 2; }
set -Eeuo pipefail
trap 'echo "ERROR at line $LINENO"; exit 1' ERR
set -x

# --- Config ---
BIN="${BIN:-./bin/sdl_reader_cli}"
DEST="${DEST:-./bundle}"
SYSROOT="${SYSROOT:-/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc}"
# Check if DEST ends with /lib (meaning we want libs directly in DEST)
if [[ "$DEST" == */lib ]]; then
    BINDIR="$(dirname "$DEST")/bin"
    LIBDIR="$DEST"
else
    BINDIR="$DEST/bin"
    LIBDIR="$DEST/lib"
fi

# Exclude:
# - glibc core (must use device versions)
# - system dynamic loader
# - system SDL2 (use device's SDL2 which has the right KMSDRM/fbcon backends)
# - ICU libraries (not needed with our minimal libarchive build)
# If the device also provides SDL2_ttf, you can optionally add ^libSDL2_ttf-2\.0\.so\. here too.
EXCL_REGEX='(^ld-linux-|^libc\.so\.|^libpthread\.so\.|^libm\.so\.|^librt\.so\.|^libdl\.so\.|^libnsl\.so\.|^libresolv\.so\.|^libSDL2-2\.0\.so\.|^libicu)'

SYSROOT_SRC=()
for p in "$SYSROOT/lib" "$SYSROOT/lib64" "$SYSROOT/usr/lib" "$SYSROOT/usr/lib64" "$SYSROOT/usr/lib/aarch64-nextui-linux-gnu" "$SYSROOT/lib/aarch64-nextui-linux-gnu" "$SYSROOT/usr/lib/aarch64-linux-gnu" "$SYSROOT/lib/aarch64-linux-gnu"; do
  [ -d "$p" ] && SYSROOT_SRC+=("$p")
done

echo "START bundling from: $PWD"
echo "Binary: $BIN"
echo "Dest:   $DEST"
echo "Sysroot: $SYSROOT"

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

  # prefer sysroot copy if available (avoids pulling host glibc builds)
  for root in "${SYSROOT_SRC[@]}"; do
    if [ -f "$root/$base" ]; then
      src="$root/$base"
      break
    fi
  done

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

  # if it's a symlink, also copy the real file it points to
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

# Optional: prune heavy libs you likely don't need on the Brick (set PRUNE_LIBS=1)
if [[ "${PRUNE_LIBS:-0}" -eq 1 ]]; then
  rm -f "$LIBDIR"/libpulse*.so* "$LIBDIR"/libsystemd*.so* "$LIBDIR"/libdbus-1*.so* || true
  rm -f "$LIBDIR"/libX*.so* "$LIBDIR"/libSM*.so* "$LIBDIR"/libICE*.so* "$LIBDIR"/libwayland-*.so* || true
  # Remove ICU and XML libraries (not needed with minimal libarchive)
  rm -f "$LIBDIR"/libicu*.so* "$LIBDIR"/libxml2*.so* || true
fi

# Size trim (optional)
strip "$BINDIR/sdl_reader_cli" 2>/dev/null || true
strip --strip-unneeded "$LIBDIR"/*.so* 2>/dev/null || true

echo "DONE. Bundle ready: $DEST"
ls -la "$DEST" "$BINDIR" "$LIBDIR" || true
