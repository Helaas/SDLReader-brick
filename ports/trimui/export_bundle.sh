#!/usr/bin/env bash
# Export unified TrimUI bundle - creates a single PAK with both TG5040 and TG5050 binaries
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$SCRIPT_DIR/pak"

echo "==================================================================="
echo "Exporting unified TrimUI bundle (TG5040 + TG5050)..."
echo "==================================================================="
echo "Project root: $PROJECT_ROOT"
echo "Bundle destination: $BUNDLE_DIR"
echo ""

# Clean and create bundle structure
rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR/bin/tg5040" "$BUNDLE_DIR/bin/tg5050" \
         "$BUNDLE_DIR/lib/tg5040" "$BUNDLE_DIR/lib/tg5050" \
         "$BUNDLE_DIR/fonts" "$BUNDLE_DIR/res"

echo "-------------------------------------------------------------------"
echo "Step 1: Copying TG5040 binaries and libraries..."
echo "-------------------------------------------------------------------"

# Copy TG5040 binary
if [ -f "$PROJECT_ROOT/build/tg5040/sdl_reader_cli" ]; then
    echo "✓ Copying TG5040 binary..."
    cp "$PROJECT_ROOT/build/tg5040/sdl_reader_cli" "$BUNDLE_DIR/bin/tg5040/"
    chmod +x "$BUNDLE_DIR/bin/tg5040/sdl_reader_cli"
else
    echo "ERROR: TG5040 binary not found at build/tg5040/sdl_reader_cli"
    echo "Run 'make tg5040' first to build the TG5040 binary"
    exit 1
fi

# Generate TG5040 library bundle using make_bundle.sh (in Docker if needed)
echo "Generating TG5040 library dependencies..."
cd "$PROJECT_ROOT"

# Check if ldd is available (means we're on Linux or in Docker)
if command -v ldd >/dev/null 2>&1; then
    # Run make_bundle.sh directly
    TG5040_TEMP=$(mktemp -d)
    export BIN="./build/tg5040/sdl_reader_cli"
    export DEST="$TG5040_TEMP"
    bash "$PROJECT_ROOT/ports/tg5040/make_bundle.sh" > /dev/null 2>&1
    cp -a "$TG5040_TEMP/lib/"* "$BUNDLE_DIR/lib/tg5040/"
    rm -rf "$TG5040_TEMP"
    echo "✓ Copied TG5040 libraries"
else
    # Run in Docker container
    echo "  (Running in TG5040 Docker container...)"
    docker run --rm -v "$PROJECT_ROOT":/workspace ghcr.io/loveretro/tg5040-toolchain:latest \
        /bin/bash -c "cd /workspace && \
        TEMP=\$(mktemp -d) && \
        BIN=./build/tg5040/sdl_reader_cli DEST=\$TEMP bash ports/tg5040/make_bundle.sh > /dev/null 2>&1 && \
        cp -a \$TEMP/lib/* ports/trimui/pak/lib/tg5040/ && \
        rm -rf \$TEMP"
    echo "✓ Copied TG5040 libraries"
fi

echo "-------------------------------------------------------------------"
echo "Step 2: Copying TG5050 binaries and libraries..."
echo "-------------------------------------------------------------------"

# Copy TG5050 binary
if [ -f "$PROJECT_ROOT/build/tg5050/sdl_reader_cli" ]; then
    echo "✓ Copying TG5050 binary..."
    cp "$PROJECT_ROOT/build/tg5050/sdl_reader_cli" "$BUNDLE_DIR/bin/tg5050/"
    chmod +x "$BUNDLE_DIR/bin/tg5050/sdl_reader_cli"
else
    echo "ERROR: TG5050 binary not found at build/tg5050/sdl_reader_cli"
    echo "Run 'make tg5050' first to build the TG5050 binary"
    exit 1
fi

# Generate TG5050 library bundle using make_bundle.sh (in Docker if needed)
echo "Generating TG5050 library dependencies..."
cd "$PROJECT_ROOT"

# Check if ldd is available (means we're on Linux or in Docker)
if command -v ldd >/dev/null 2>&1; then
    # Run make_bundle.sh directly
    TG5050_TEMP=$(mktemp -d)
    export BIN="./build/tg5050/sdl_reader_cli"
    export DEST="$TG5050_TEMP"
    bash "$PROJECT_ROOT/ports/tg5050/make_bundle.sh" > /dev/null 2>&1
    cp -a "$TG5050_TEMP/lib/"* "$BUNDLE_DIR/lib/tg5050/"
    rm -rf "$TG5050_TEMP"
    echo "✓ Copied TG5050 libraries"
else
    # Run in Docker container
    echo "  (Running in TG5050 Docker container...)"
    docker run --rm -v "$PROJECT_ROOT":/workspace ghcr.io/loveretro/tg5050-toolchain:latest \
        /bin/bash -c "cd /workspace && \
        TEMP=\$(mktemp -d) && \
        BIN=./build/tg5050/sdl_reader_cli DEST=\$TEMP bash ports/tg5050/make_bundle.sh > /dev/null 2>&1 && \
        cp -a \$TEMP/lib/* ports/trimui/pak/lib/tg5050/ && \
        rm -rf \$TEMP"
    echo "✓ Copied TG5050 libraries"
fi

echo "-------------------------------------------------------------------"
echo "Step 3: Copying shared resources..."
echo "-------------------------------------------------------------------"

# Copy template files (launch.sh and docs.pdf)
TEMPLATE_DIR="$PROJECT_ROOT/ports/trimui/pak-template"
if [ -f "$TEMPLATE_DIR/launch.sh" ]; then
    echo "✓ Copying launch.sh from template..."
    cp "$TEMPLATE_DIR/launch.sh" "$BUNDLE_DIR/"
    chmod +x "$BUNDLE_DIR/launch.sh"
else
    echo "ERROR: launch.sh not found in pak-template/"
    exit 1
fi

if [ -f "$TEMPLATE_DIR/res/docs.pdf" ]; then
    echo "✓ Copying docs.pdf from template..."
    cp "$TEMPLATE_DIR/res/docs.pdf" "$BUNDLE_DIR/res/"
else
    echo "Warning: docs.pdf not found in pak-template/res/"
fi

# Copy fonts
if [ -d "$PROJECT_ROOT/fonts" ]; then
    echo "✓ Copying fonts..."
    cp -a "$PROJECT_ROOT/fonts/." "$BUNDLE_DIR/fonts/"
else
    echo "Warning: fonts/ directory not found"
fi

# Copy other resources if they exist
if [ -d "$PROJECT_ROOT/res" ]; then
    echo "✓ Copying resources..."
    # Don't overwrite docs.pdf if it exists
    for item in "$PROJECT_ROOT/res"/*; do
        if [ -e "$item" ]; then
            basename=$(basename "$item")
            if [ "$basename" != "docs.pdf" ] || [ ! -f "$BUNDLE_DIR/res/docs.pdf" ]; then
                cp -a "$item" "$BUNDLE_DIR/res/"
            fi
        fi
    done
fi

# Copy documentation files
echo "✓ Copying documentation..."
cp "$PROJECT_ROOT/README.md" "$BUNDLE_DIR/"
cp "$PROJECT_ROOT/pak.json" "$BUNDLE_DIR/"

echo "-------------------------------------------------------------------"
echo "Step 4: Creating SDLReader.pak.zip..."
echo "-------------------------------------------------------------------"

# Zip the bundle with maximum compression
if ! command -v zip &> /dev/null; then
    echo "ERROR: zip command not found. Install zip first."
    exit 1
fi

rm -f "$PROJECT_ROOT/SDLReader.pak.zip"
cd "$BUNDLE_DIR"
zip -9 -r "$PROJECT_ROOT/SDLReader.pak.zip" . > /dev/null

echo ""
echo "==================================================================="
echo "✓ Unified TrimUI bundle exported successfully!"
echo "==================================================================="
echo "Bundle directory: $BUNDLE_DIR"
echo "Zipped bundle: $PROJECT_ROOT/SDLReader.pak.zip"
echo ""
echo "Bundle structure:"
find "$BUNDLE_DIR" -type f | sort | sed 's|^.*/pak/|  |'
echo ""
echo "This PAK contains binaries for both TG5040 and TG5050 platforms."
echo "The launcher will automatically detect the platform at runtime."
