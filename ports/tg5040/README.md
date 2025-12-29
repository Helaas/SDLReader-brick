# TG5040 Port - Docker Development Environment

This directory contains the TG5040-specific build configuration and Docker development environment for the SDLReader project. It supports both the TrimUI Brick and TrimUI Smart Pro devices.

Powered by the prebuilt [tg5040-toolchain](https://github.com/LoveRetro/tg5040-toolchain) container published to GHCR.

## Key Features

- **Built-in CBR Support**: Now includes support for CBR (Comic Book RAR) files alongside CBZ/ZIP comic books
- **WebP Image Support**: Enhanced WebP format support in PDF documents and comic book archives
- **Self-Contained MuPDF**: Automatically downloads and builds MuPDF 1.26.7 with custom libarchive and WebP support
- **Bundled libwebp**: Builds a static libwebp (1.3.2) inside the workspace to provide WebP decode/demux without system packages in the toolchain container
- **Custom libarchive**: Builds minimal libarchive without ICU/XML dependencies for optimal bundle size
- **Hardware Power Management**: NextUI-compatible power button handling
- **Complete Bundle Export**: Creates self-contained distribution packages
- **Integrated Nuklear UI**: Controller-first file browser, font picker, reading-style themes, and on-screen number pad
- **Persistent Preferences**: Ships with curated fonts, `config.json` (user-managed), and automatic `reading_history.json` resume support

## Files in this directory
- `Makefile` - TG5040 application build configuration
- `Makefile.docker` - Docker environment management
- `docker-compose.yml` - Docker Compose setup
- `Dockerfile` - TG5040 toolchain container image
- `export_bundle.sh` - Bundle export script for distribution packages
- `make_bundle.sh` - Library dependency bundling script (called from `export_bundle.sh`)
- `BUNDLE_EXPORT.md` - Detailed bundle export system documentation

## Quick Start

**Auto-detected builds**: The top-level `Makefile` now notices when it runs inside this Docker environment (or any container that exposes `/.dockerenv`) and defaults to the TG5040 target. Plain `make` inside the container will therefore build the TG5040 port. When building from the host, continue to use `make tg5040` (or `make PLATFORM=tg5040`) to opt in explicitly.

### Using Docker Compose (Recommended)
```bash
cd ports/tg5040

# Ensure the latest toolchain image is available
docker compose pull dev

# Start a shell with the prebuilt toolchain (ghcr.io/loveretro/tg5040-toolchain)
docker compose run --rm dev bash

# Build the application (inside container)
cd /workspace
make            # auto-selects the TG5040 target when run in the container
```

### Using Docker Makefile
```bash
cd ports/tg5040

# Pull toolchain and enter shell
make -f Makefile.docker shell

# Build the application (inside container)
cd /workspace
make            # auto-selects the TG5040 target when run in the container
```

### Direct Build (with toolchain installed)
```bash
# From project root (outside Docker)
make tg5040
# or
make PLATFORM=tg5040

# Build and export TG5040 bundle
make export-tg5040
```

## Bundle Export

The TG5040 port includes an automated bundle export system that creates a complete distribution package:

```bash
# Export complete TG5040 bundle
make export-tg5040

# Manual export from ports/tg5040 directory
./export_bundle.sh
```

The exported bundle (`ports/tg5040/pak/`) contains:
- **bin/**: `sdl_reader_cli` (legacy utilities such as `jq`/`minui-list` are preserved if present before export)
- **lib/**: All required shared library dependencies with embedded RPATHs
- **fonts/**: Bundled fonts for the runtime picker (Inter, JetBrains Mono, Noto Serif Condensed, Roboto)
- **res/**: Optional resource files (bundled docs, etc.)
- **launch.sh**: Launcher script that invokes `./bin/sdl_reader_cli --browse`
- **README.md / pak.json**: Copied for convenience inside the bundle

## Applied Patches

When you run `make tg5040` or `make export-tg5040`, the build system applies `webp-upstream-697749.patch` (MuPDF) to backport WebP decoding fixes from KOReader (shared with other platforms). The updated GHCR toolchain ships a modern SDL2 stack, so no Nuklear SDL renderer compatibility patch is required; we use the upstream renderer from Nuklear 4.12.8.

### Bundle Features
- **Self-contained**: Includes all dependencies and resources
- **File preservation**: Preserves important utilities and scripts during rebuild
- **Optimized**: Excludes system libraries, strips debug symbols
- **Smart dependency resolution**: Uses `ldd` analysis with exclusion filters

## Installation

With Docker installed and running, `make -f Makefile.docker shell` pulls the prebuilt toolchain image (`ghcr.io/loveretro/tg5040-toolchain`) and drops into a shell inside the container. The container's `/workspace` path is bound to the project root by default. The cross toolchain is located at `/opt/aarch64-nextui-linux-gnu` inside the container.

Because the image is prebuilt, the shell target simply pulls updates and starts the container—no local image build step is required.

## Development Workflow

- **Host machine**: Edit source code in the project root (`../../src/`, `../../include/`, etc.)
- **Container**: Build and test inside the Docker container
- **Volume mapping**: The project root is mounted at `/workspace` inside the container
- **Toolchain**: Located at `/opt/aarch64-nextui-linux-gnu` inside the container

Runtime settings (`config.json`) and reading progress (`reading_history.json`) are generated inside the reader state directory (`$SDL_READER_STATE_DIR`, set by `launch.sh`). The launcher also sets `SDL_READER_DEFAULT_DIR=/mnt/SDCARD` so the browser never leaves the SD card root. Both files are ignored by Git so you can modify them freely on the device or within the container.

### Fonts & Reading Styles

Drop additional `.ttf` or `.otf` files into `ports/tg5040/pak/fonts/` (or the project-root `fonts/` folder before exporting). When you launch the bundle, the Options → Font & Reading Style menu automatically discovers those fonts so you can preview and select them on-device.

### Container Details
- The container's `/workspace` is mapped to the project root directory
- Source code changes on the host are immediately available in the container
- Built artifacts are stored in the project's `bin/` and `build/` directories

## Platform-Specific Features
The TG5040 build (TrimUI Brick & Smart Pro) includes:
- **Nuklear UI Stack**: Built-in browser launched via `--browse`, font & reading-style menu, controller number pad, and persisted `reading_history.json`
  - Toggle the new thumbnail grid with the **X** button for cover previews rendered asynchronously.
  - Control the zoom minimap overlay via the `showDocumentMinimap` flag in `config.json`.
- **Advanced Hardware Power Management**: NextUI-compatible power button handling
  - Power button monitoring via `/dev/input/event1`
  - Short press: Intelligent sleep with fake sleep fallback
    - Attempts real hardware sleep first
    - If hardware sleep fails, enters fake sleep mode (black screen, input blocking)
    - Continues attempting deep sleep in background every 2 seconds
    - Automatically exits fake sleep when deep sleep succeeds
    - Manual wake via power button press during fake sleep
  - Long press (2+ seconds): Safe system shutdown
  - Smart error handling: 30-second timeout before showing user errors
  - Event flushing on wake to prevent phantom button presses
  - Wake detection with proper state management
- **Document Format Support**:
  - PDF documents via MuPDF integration
  - CBZ/ZIP comic book archives via MuPDF native support
  - CBR comic book archives via custom-built minimal libarchive (no ICU dependencies)
  - EPUB e-books via MuPDF native support
  - Plain text files (.txt) with configurable font size, face, and reading style
  - **WebP images**: Enhanced WebP format support within documents and archives
- **Platform-optimized build flags**: `-DTG5040_PLATFORM`
- **Port-specific source structure**:
  - `include/power_handler.h` - TG5040 power management interface
  - `src/power_handler.cpp` - Hardware-specific power button implementation with NextUI compatibility
- **Embedded Linux-specific libraries and dependencies**
  - SDL2, SDL2_ttf for graphics and input
  - Self-built MuPDF 1.26.7 with custom libarchive for CBR support and WebP for enhanced image format support (no ICU dependencies)
  - Document rendering support: PDF, CBZ, CBR, XPS, EPUB with WebP image support

See [setup-env.sh](./support/setup-env.sh) for some useful vars for compiling that are exported automatically.

## Docker for Mac

Docker for Mac has a memory limit that can make the toolchain build fail. Follow [these instructions](https://docs.docker.com/docker-for-mac/) to increase the memory limit.
