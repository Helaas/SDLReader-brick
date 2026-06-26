#!/usr/bin/env bash
# Export MLP1 bundle - creates a complete distribution .pak directory
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$SCRIPT_DIR/pak"

echo "Exporting MLP1 bundle..."
echo "Project root: $PROJECT_ROOT"
echo "Bundle destination: $BUNDLE_DIR"

# Clean and create bundle structure.
# NOTE: launch.sh and pak.json are committed source files that live directly in
# $BUNDLE_DIR, so only the generated subdirectories are removed (never the whole
# directory, which would delete those tracked inputs).
rm -rf "$BUNDLE_DIR/bin" "$BUNDLE_DIR/lib" "$BUNDLE_DIR/res" "$BUNDLE_DIR/fonts"
mkdir -p "$BUNDLE_DIR/bin" "$BUNDLE_DIR/lib" "$BUNDLE_DIR/res" "$BUNDLE_DIR/fonts"

# Copy main binary from build directory
if [ -f "$PROJECT_ROOT/build/mlp1/sdl_reader_cli" ]; then
    echo "Copying sdl_reader_cli binary..."
    cp "$PROJECT_ROOT/build/mlp1/sdl_reader_cli" "$BUNDLE_DIR/bin/"
else
    echo "Warning: sdl_reader_cli not found in build/mlp1/ - did you run 'make mlp1'?"
fi

# Generate library bundle using make_bundle.sh
echo "Generating library dependencies..."
cd "$PROJECT_ROOT"
export BIN="./build/mlp1/sdl_reader_cli"
export DEST="./ports/mlp1/pak/lib"
if [ -f "$SCRIPT_DIR/make_bundle.sh" ]; then
    bash "$SCRIPT_DIR/make_bundle.sh"
else
    echo "Warning: make_bundle.sh not found in $SCRIPT_DIR"
fi

# Copy fonts
if [ -d "$PROJECT_ROOT/fonts" ]; then
    echo "Copying fonts..."
    cp -a "$PROJECT_ROOT/fonts/." "$BUNDLE_DIR/fonts/"
else
    echo "Warning: fonts/ directory not found"
fi

# Copy app icon
if [ -f "$PROJECT_ROOT/res/icon.png" ]; then
    echo "Copying icon..."
    cp "$PROJECT_ROOT/res/icon.png" "$BUNDLE_DIR/res/icon.png"
fi

# Copy launch script
if [ -f "$SCRIPT_DIR/pak/launch.sh" ]; then
    echo "Copying launch.sh..."
    cp "$SCRIPT_DIR/pak/launch.sh" "$BUNDLE_DIR/"
    chmod +x "$BUNDLE_DIR/launch.sh"
fi

# Copy MLP1-specific pak.json
if [ -f "$SCRIPT_DIR/pak/pak.json" ]; then
    echo "Copying pak.json..."
    cp "$SCRIPT_DIR/pak/pak.json" "$BUNDLE_DIR/"
fi

# Make all binaries executable
chmod +x "$BUNDLE_DIR/bin"/* 2>/dev/null || true

echo ""
echo "MLP1 bundle exported successfully to: $BUNDLE_DIR"
echo "Bundle contents:"
find "$BUNDLE_DIR" -type f | sort
