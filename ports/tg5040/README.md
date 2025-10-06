# TG5040 Port - Docker Development Environment

This directory contains the TG5040-specific build configuration and Docker development environment for the SDLReader project.

Based on the [Trimui toolchain Docker image](https://git.crowdedwood.com/trimui-toolchain/) by neonloop.

## Key Features

- **Built-in CBR Support**: Now includes support for CBR (Comic Book RAR) files alongside CBZ/ZIP comic books
- **WebP Image Support**: Enhanced WebP format support in PDF documents and comic book archives
- **Self-Contained MuPDF**: Automatically downloads and builds MuPDF 1.26.7 with custom libarchive and WebP support
- **Custom libarchive**: Builds minimal libarchive without ICU/XML dependencies for optimal bundle size
- **Hardware Power Management**: NextUI-compatible power button handling
- **Complete Bundle Export**: Creates self-contained distribution packages
- **Integrated ImGui UI**: Controller-first file browser, font picker, reading-style themes, and on-screen number pad
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

### Using Docker Compose (Recommended)
```bash
cd ports/tg5040

# Start development environment
docker-compose up -d dev

# Enter the container
docker-compose exec dev bash

# Build the application (inside container)
cd /root/workspace
make tg5040
```

### Using Docker Makefile
```bash
cd ports/tg5040

# Build toolchain and enter shell
make -f Makefile.docker shell

# Build the application (inside container)
cd /root/workspace
make tg5040
```

### Direct Build (with toolchain installed)
```bash
# From project root
make tg5040

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
- **lib/**: All required shared library dependencies with patched RPATHs
- **fonts/**: Bundled fonts for the runtime picker (Inter, JetBrains Mono, Noto Serif Condensed, Roboto)
- **res/**: Optional resource files (bundled docs, etc.)
- **launch.sh**: Launcher script that invokes `./bin/sdl_reader_cli --browse`
- **README.md / pak.json**: Copied for convenience inside the bundle

## Applied Patches

When you run `make tg5040` or `make export-tg5040`, the build system applies several patches automatically:

- `webp-upstream-697749.patch` (MuPDF) — shared with other platforms to backport WebP decoding fixes from KOReader.
- `patches/imgui_impl_sdlrenderer_legacy.patch` — replaces the Dear ImGui SDL renderer backend with a legacy-friendly version that works on the TG5040's SDL 2.0.10 stack and adds caching optimizations for the low-power GPU.
- `patches/imgui_swap_ab_buttons_tg5040.patch` — swaps the reported A/B buttons so Dear ImGui's activation/cancel affordances match the TrimUI Brick's physical layout.

### Bundle Features
- **Self-contained**: Includes all dependencies and resources
- **File preservation**: Preserves important utilities and scripts during rebuild
- **Optimized**: Excludes system libraries, strips debug symbols
- **Smart dependency resolution**: Uses `ldd` analysis with exclusion filters

## Installation

With Docker installed and running, `make shell` builds the toolchain and drops into a shell inside the container. The container's `~/workspace` is bound to `./workspace` by default. The toolchain is located at `/opt/` inside the container.

After building the first time, unless a dependency of the image has changed, `make shell` will skip building and drop into the shell.

## Development Workflow

- **Host machine**: Edit source code in the project root (`../../src/`, `../../include/`, etc.)
- **Container**: Build and test inside the Docker container
- **Volume mapping**: The project root is mounted at `/root/workspace` inside the container
- **Toolchain**: Located at `/opt/` inside the container

Runtime settings (`config.json`) and reading progress (`reading_history.json`) are generated next to the executable. They are ignored by Git so you can modify them freely on the device or within the container.

### Fonts & Reading Styles

Drop additional `.ttf` or `.otf` files into `ports/tg5040/pak/fonts/` (or the project-root `fonts/` folder before exporting). When you launch the bundle, the Options → Font & Reading Style menu automatically discovers those fonts so you can preview and select them on-device.

### Container Details
- The container's `/root/workspace` is mapped to the project root directory
- Source code changes on the host are immediately available in the container
- Built artifacts are stored in the project's `bin/` and `build/` directories

## Platform-Specific Features
The TG5040 build includes:
- **ImGui UI Stack**: Built-in browser launched via `--browse`, font & reading-style menu, controller number pad, and persisted `reading_history.json`
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
