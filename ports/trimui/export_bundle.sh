#!/usr/bin/env bash
# Export TrimUI .pakz bundle - creates self-contained PAKs for each platform
# Output: SDLReader.pakz containing Tools/tg5040/SDLReader.pak/ and Tools/tg5050/SDLReader.pak/
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
STAGING_DIR="$SCRIPT_DIR/staging"
TEMPLATE_DIR="$SCRIPT_DIR/pak-template"
OUTPUT_FILE="$PROJECT_ROOT/SDLReader.pakz"

PLATFORMS="${PLATFORMS:-tg5040 tg5050 my355}"
BINARY_PLATFORM="${BINARY_PLATFORM:-}"
BUNDLE_PLATFORM="${BUNDLE_PLATFORM:-}"
TOOLCHAIN_IMAGE="${TOOLCHAIN_IMAGE:-}"
BUNDLE_LIBS="${BUNDLE_LIBS:-1}"

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
    local binary_platform="${BINARY_PLATFORM:-$platform}"
    local bundle_platform="${BUNDLE_PLATFORM:-$platform}"
    local toolchain_image="${TOOLCHAIN_IMAGE:-ghcr.io/loveretro/${bundle_platform}-toolchain:latest}"

    echo "  Generating library dependencies..."
    cd "$PROJECT_ROOT"

    if command -v ldd >/dev/null 2>&1; then
        local temp_dir
        temp_dir=$(mktemp -d)
        BIN="./build/$binary_platform/sdl_reader_cli" DEST="$temp_dir" PRUNE_LIBS=1 \
            bash "$PROJECT_ROOT/ports/$bundle_platform/make_bundle.sh" > /dev/null 2>&1
        cp -a "$temp_dir/lib/"* "$pak_dir/lib/"
        rm -rf "$temp_dir"
    else
        echo "  (Running dependency bundler in Docker...)"
        docker run --rm -v "$PROJECT_ROOT":/workspace "$toolchain_image" \
            /bin/bash -c "cd /workspace && \
            TEMP=\$(mktemp -d) && \
            BIN=./build/$binary_platform/sdl_reader_cli DEST=\$TEMP PRUNE_LIBS=1 bash ports/$bundle_platform/make_bundle.sh > /dev/null 2>&1 && \
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
    mkdir -p "$PAK_DIR/bin" "$PAK_DIR/fonts" "$PAK_DIR/res"

    # Binary
    SOURCE_PLATFORM="${BINARY_PLATFORM:-$platform}"
    if [ -f "$PROJECT_ROOT/build/$SOURCE_PLATFORM/sdl_reader_cli" ]; then
        cp "$PROJECT_ROOT/build/$SOURCE_PLATFORM/sdl_reader_cli" "$PAK_DIR/bin/"
        chmod +x "$PAK_DIR/bin/sdl_reader_cli"
        echo "  Copied binary"
    else
        echo "ERROR: Binary not found at build/$SOURCE_PLATFORM/sdl_reader_cli"
        echo "Run the matching build target first"
        exit 1
    fi

    # Strip binary (needs cross-strip when run from host)
    strip_binary() {
        local bin="$1"
        local plat="$2"
        local size_before size_after
        size_before=$(stat -c%s "$bin" 2>/dev/null || stat -f%z "$bin")
        if [ -n "$TOOLCHAIN_IMAGE" ]; then
            docker run --rm -v "$PROJECT_ROOT":/workspace "$TOOLCHAIN_IMAGE" \
                strip "/workspace/${bin#$PROJECT_ROOT/}"
        elif command -v ldd >/dev/null 2>&1; then
            strip "$bin"
        else
            local image="ghcr.io/loveretro/${plat}-toolchain:latest"
            docker run --rm -v "$PROJECT_ROOT":/workspace "$image" \
                strip "/workspace/${bin#$PROJECT_ROOT/}"
        fi
        size_after=$(stat -c%s "$bin" 2>/dev/null || stat -f%z "$bin")
        echo "  Stripped binary: $((size_before / 1048576))MB -> $((size_after / 1048576))MB"
    }
    strip_binary "$PAK_DIR/bin/sdl_reader_cli" "$platform"

    # Legacy per-platform exports keep their private libraries. The universal
    # build uses each device's native runtime to avoid GPU/zlib ABI conflicts.
    if [ "$BUNDLE_LIBS" = "1" ]; then
        mkdir -p "$PAK_DIR/lib"
        bundle_libs "$platform" "$PAK_DIR"
        echo "  Copied libraries"
    fi

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

if [ -n "$BINARY_PLATFORM" ]; then
    reference="$STAGING_DIR/Tools/tg5040/SDLReader.pak/bin/sdl_reader_cli"
    for platform in $PLATFORMS; do
        cmp -s "$reference" "$STAGING_DIR/Tools/$platform/SDLReader.pak/bin/sdl_reader_cli"
    done
    echo "Verified one identical executable in all platform trees."
fi

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
