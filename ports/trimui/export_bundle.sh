#!/usr/bin/env bash
# Export TrimUI .pakz bundle - creates self-contained PAKs for each platform
# Output: SDLReader.pakz containing Tools/tg5040/SDLReader.pak/ and Tools/tg5050/SDLReader.pak/
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
STAGING_DIR="$SCRIPT_DIR/staging"
TEMPLATE_DIR="$SCRIPT_DIR/pak-template"
OUTPUT_FILE="$PROJECT_ROOT/SDLReader.pakz"

PLATFORMS="tg5040 tg5050"

echo "==================================================================="
echo "Exporting TrimUI .pakz bundle..."
echo "==================================================================="
echo ""

# Clean staging area
rm -rf "$STAGING_DIR"

# Helper: bundle libraries for a platform (uses Docker if ldd unavailable)
bundle_libs() {
    local platform="$1"
    local pak_dir="$2"

    echo "  Generating library dependencies..."
    cd "$PROJECT_ROOT"

    if command -v ldd >/dev/null 2>&1; then
        local temp_dir
        temp_dir=$(mktemp -d)
        BIN="./build/$platform/sdl_reader_cli" DEST="$temp_dir" \
            bash "$PROJECT_ROOT/ports/$platform/make_bundle.sh" > /dev/null 2>&1
        cp -a "$temp_dir/lib/"* "$pak_dir/lib/"
        rm -rf "$temp_dir"
    else
        echo "  (Running in $platform Docker container...)"
        docker run --rm -v "$PROJECT_ROOT":/workspace "ghcr.io/loveretro/${platform}-toolchain:latest" \
            /bin/bash -c "cd /workspace && \
            TEMP=\$(mktemp -d) && \
            BIN=./build/$platform/sdl_reader_cli DEST=\$TEMP bash ports/$platform/make_bundle.sh > /dev/null 2>&1 && \
            mkdir -p ports/trimui/staging/Tools/$platform/SDLReader.pak/lib && \
            cp -a \$TEMP/lib/* ports/trimui/staging/Tools/$platform/SDLReader.pak/lib/ && \
            rm -rf \$TEMP"
    fi
}

for platform in $PLATFORMS; do
    echo "-------------------------------------------------------------------"
    echo "Bundling $platform..."
    echo "-------------------------------------------------------------------"

    PAK_DIR="$STAGING_DIR/Tools/$platform/SDLReader.pak"
    mkdir -p "$PAK_DIR/bin" "$PAK_DIR/lib" "$PAK_DIR/fonts" "$PAK_DIR/res"

    # Binary
    if [ -f "$PROJECT_ROOT/build/$platform/sdl_reader_cli" ]; then
        cp "$PROJECT_ROOT/build/$platform/sdl_reader_cli" "$PAK_DIR/bin/"
        chmod +x "$PAK_DIR/bin/sdl_reader_cli"
        echo "  Copied binary"
    else
        echo "ERROR: Binary not found at build/$platform/sdl_reader_cli"
        echo "Run 'make $platform' first"
        exit 1
    fi

    # Libraries
    bundle_libs "$platform" "$PAK_DIR"
    echo "  Copied libraries"

    # launch.sh
    cp "$TEMPLATE_DIR/launch.sh" "$PAK_DIR/"
    chmod +x "$PAK_DIR/launch.sh"
    echo "  Copied launch.sh"

    # pak.json
    cp "$PROJECT_ROOT/pak.json" "$PAK_DIR/"
    echo "  Copied pak.json"

    # Fonts
    if [ -d "$PROJECT_ROOT/fonts" ]; then
        cp -a "$PROJECT_ROOT/fonts/." "$PAK_DIR/fonts/"
        echo "  Copied fonts"
    fi

    # docs.pdf (for first-run experience)
    if [ -f "$TEMPLATE_DIR/res/docs.pdf" ]; then
        cp "$TEMPLATE_DIR/res/docs.pdf" "$PAK_DIR/res/"
        echo "  Copied res/docs.pdf"
    fi

    echo ""
done

echo "-------------------------------------------------------------------"
echo "Creating SDLReader.pakz..."
echo "-------------------------------------------------------------------"

if ! command -v zip &> /dev/null; then
    echo "ERROR: zip command not found."
    exit 1
fi

rm -f "$OUTPUT_FILE"
cd "$STAGING_DIR"
zip -9 -r "$OUTPUT_FILE" . > /dev/null

echo ""
echo "==================================================================="
echo "SDLReader.pakz exported successfully!"
echo "==================================================================="
echo ""
echo "Output: $OUTPUT_FILE"
echo ""
echo "Contents:"
find "$STAGING_DIR" -type f | sort | sed "s|$STAGING_DIR/||"
echo ""

# Clean up staging
rm -rf "$STAGING_DIR"
