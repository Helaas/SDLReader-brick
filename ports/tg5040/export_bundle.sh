#!/usr/bin/env bash
# Export TG5040 bundle - creates a complete distribution package
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUNDLE_DIR="$SCRIPT_DIR/pak"

echo "Exporting TG5040 bundle..."
echo "Project root: $PROJECT_ROOT"
echo "Bundle destination: $BUNDLE_DIR"

# Clean and create bundle structure
rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR/bin" "$BUNDLE_DIR/lib" "$BUNDLE_DIR/res"

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
    echo "Warning: sdl_reader_cli not found in bin/ - did you run 'make tg5040'?"
fi

# Generate library bundle using make_bundle2.sh
echo "Generating library dependencies..."
cd "$PROJECT_ROOT"
export BIN="./bin/sdl_reader_cli"
export DEST="./lib"
if [ -f "$SCRIPT_DIR/make_bundle2.sh" ]; then
    bash "$SCRIPT_DIR/make_bundle2.sh"
    
    # Copy the generated lib folder to bundle
    if [ -d "$PROJECT_ROOT/lib" ]; then
        echo "Copying library dependencies..."
        cp -a "$PROJECT_ROOT/lib/." "$BUNDLE_DIR/lib/"
    fi
else
    echo "Warning: make_bundle2.sh not found in $SCRIPT_DIR"
fi

# Copy resources
if [ -d "$PROJECT_ROOT/res" ]; then
    echo "Copying resources..."
    cp -a "$PROJECT_ROOT/res/." "$BUNDLE_DIR/res/"
else
    echo "Warning: res/ directory not found"
fi

# Make all binaries executable
chmod +x "$BUNDLE_DIR/bin"/* 2>/dev/null || true

echo ""
echo "TG5040 bundle exported successfully to: $BUNDLE_DIR"
echo "Bundle contents:"
find "$BUNDLE_DIR" -type f | sort
