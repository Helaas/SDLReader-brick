#!/usr/bin/env bash
set -euo pipefail

BIN=./sdl_reader_cli
DEST=./bundle
mkdir -p "$DEST/bin" "$DEST/lib"

# copy binary
cp -f "$BIN" "$DEST/bin/"

# libs to exclude (from system)
EXCL='(^libc\.so\.|^ld-linux-|^libm\.so\.|^libpthread\.so\.|^librt\.so\.|^libdl\.so\.|^libnsl\.so\.|^libresolv\.so\.)'

# collect from ldd
ldd "$BIN" | awk '
  /=>/ && $3 ~ /^\// { print $3 }
  /^[[:space:]]*\/.*\.so/ { print $1 }
' | sort -u | while read -r lib; do
  base="$(basename "$lib")"
  if echo "$base" | grep -Eq "$EXCL"; then
    continue
  fi
  cp -u "$lib" "$DEST/lib/" || true
  # if it’s a symlink, also copy the real file
  if [ -L "$lib" ]; then
    tgt="$(readlink -f "$lib")"
    cp -u "$tgt" "$DEST/lib/" || true
  fi
done

# optional: also copy deps of the libs themselves (one extra pass)
for so in "$DEST/lib/"*.so*; do
  ldd "$so" 2>/dev/null | awk '
    /=>/ && $3 ~ /^\// { print $3 }
    /^[[:space:]]*\/.*\.so/ { print $1 }
  ' | while read -r lib; do
    [ -f "$lib" ] || continue
    base="$(basename "$lib")"
    if echo "$base" | grep -Eq "$EXCL"; then
      continue
    fi
    cp -u "$lib" "$DEST/lib/" || true
    if [ -L "$lib" ]; then
      tgt="$(readlink -f "$lib")"
      cp -u "$tgt" "$DEST/lib/" || true
    fi
  done
done

# size trim (optional, keeps symbols for debugging: remove --strip-unneeded to keep)
strip "$DEST/bin/sdl_reader_cli" || true
strip --strip-unneeded "$DEST/lib/"*.so* || true

echo "Bundle ready in $DEST/"
