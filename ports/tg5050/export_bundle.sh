#!/usr/bin/env bash
# Export TG5050 bundle - creates a complete distribution package
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$SCRIPT_DIR/pak"

echo "Exporting TG5050 bundle..."
echo "Project root: $PROJECT_ROOT"
echo "Bundle destination: $BUNDLE_DIR"

# Clean and create bundle structure
# Preserve important files before cleaning
TEMP_DIR=$(mktemp -d)
[ -f "$BUNDLE_DIR/bin/jq" ] && cp "$BUNDLE_DIR/bin/jq" "$TEMP_DIR/"
[ -f "$BUNDLE_DIR/bin/minui-list" ] && cp "$BUNDLE_DIR/bin/minui-list" "$TEMP_DIR/"
[ -f "$BUNDLE_DIR/launch.sh" ] && cp "$BUNDLE_DIR/launch.sh" "$TEMP_DIR/"
[ -f "$BUNDLE_DIR/res/docs.pdf" ] && cp "$BUNDLE_DIR/res/docs.pdf" "$TEMP_DIR/docs.pdf"

rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR/bin" "$BUNDLE_DIR/lib" "$BUNDLE_DIR/res"

# Restore preserved files
[ -f "$TEMP_DIR/jq" ] && cp "$TEMP_DIR/jq" "$BUNDLE_DIR/bin/"
[ -f "$TEMP_DIR/minui-list" ] && cp "$TEMP_DIR/minui-list" "$BUNDLE_DIR/bin/"
[ -f "$TEMP_DIR/launch.sh" ] && cp "$TEMP_DIR/launch.sh" "$BUNDLE_DIR/"
[ -f "$TEMP_DIR/docs.pdf" ] && cp "$TEMP_DIR/docs.pdf" "$BUNDLE_DIR/res/docs.pdf"
rm -rf "$TEMP_DIR"

# Copy bin contents from existing pak folder
if [ -d "$PROJECT_ROOT/pak/bin" ]; then
    echo "Copying existing pak/bin contents..."
    cp -a "$PROJECT_ROOT/pak/bin/." "$BUNDLE_DIR/bin/"
fi

# Copy the launch script from pak
if [ -f "$PROJECT_ROOT/pak/launch.sh" ]; then
    echo "Copying launch script..."
    cp "$PROJECT_ROOT/pak/launch.sh" "$BUNDLE_DIR/"
fi

# Copy main binary from bin directory
if [ -f "$PROJECT_ROOT/bin/sdl_reader_cli" ]; then
    echo "Copying sdl_reader_cli binary..."
    cp "$PROJECT_ROOT/bin/sdl_reader_cli" "$BUNDLE_DIR/bin/"
else
    echo "Warning: sdl_reader_cli not found in bin/ - did you run 'make tg5050'?"
fi

# Generate library bundle using make_bundle.sh
echo "Generating library dependencies..."
cd "$PROJECT_ROOT"
export BIN="./bin/sdl_reader_cli"
export DEST="./ports/tg5050/pak/lib"
if [ -f "$SCRIPT_DIR/make_bundle.sh" ]; then
    bash "$SCRIPT_DIR/make_bundle.sh"
else
    echo "Warning: make_bundle.sh not found in $SCRIPT_DIR"
fi

# Copy fonts and resources
if [ -d "$PROJECT_ROOT/fonts" ]; then
    echo "Copying fonts..."
    mkdir -p "$BUNDLE_DIR/fonts"
    cp -a "$PROJECT_ROOT/fonts/." "$BUNDLE_DIR/fonts/"
else
    echo "Warning: fonts/ directory not found"
fi

# Copy other resources if they exist
if [ -d "$PROJECT_ROOT/res" ]; then
    echo "Copying resources..."
    cp -a "$PROJECT_ROOT/res/." "$BUNDLE_DIR/res/"
    # If docs.pdf was preserved, restore it after copying
    [ -f "$TEMP_DIR/docs.pdf" ] && cp "$TEMP_DIR/docs.pdf" "$BUNDLE_DIR/res/docs.pdf"
fi

# Copy documentation files
echo "Copying documentation..."
cp "$PROJECT_ROOT/README.md" "$BUNDLE_DIR/"
cp "$PROJECT_ROOT/pak.json" "$BUNDLE_DIR/"

# Make all binaries executable
chmod +x "$BUNDLE_DIR/bin"/* 2>/dev/null || true

# Zip the bundle with maximum compression
echo "Creating SDLReader.pak.zip..."
if ! command -v zip &> /dev/null; then
    echo "Installing zip..."
    apt update && apt install -y zip
fi
rm -f "$PROJECT_ROOT/SDLReader.pak.zip"
cd "$BUNDLE_DIR"
zip -9 -r "$PROJECT_ROOT/SDLReader.pak.zip" .

echo ""
echo "TG5050 bundle exported successfully to: $BUNDLE_DIR"
echo "Zipped bundle: $PROJECT_ROOT/SDLReader.pak.zip"
echo "Bundle contents:"
find "$BUNDLE_DIR" -type f | sort
