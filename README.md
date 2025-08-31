# SDL Reader

A minimal document reader built using SDL2 and MuPDF supporting multiple document formats 

## Table of Contents
* [Features](#features)
* [Supported Document Types](#supported-document-types)
* [Build Instructions](#build-instructions)
* [Usage](#usage)
* [TG5040 Deployment](#tg5040-deployment)
* [User Inputs](#user-inputs)
* [Project Structure](#project-structure)
* [Architecture](#architecture)

## Features
* View PDF documents, comic book archives (CBZ/ZIP), and EPUB books.
* Page navigation (next/previous page).
* Jump to specific page with `G` key (enter page number and press Enter).
* Quick page jumping (±10 pages) with `[` and `]` keys.
* Zoom in/out and fit page to width.
* Page rotation (90° increments) with `Shift + R`.
* Page mirroring (horizontal/vertical) with `H` and `V` keys.
* Scroll within pages (if zoomed in or page is larger than window).
* Toggle fullscreen mode.
* Basic UI overlay showing current page and zoom level.
* Advanced power management for embedded devices (TG5040).

## Supported Document Types
* **PDF** (`.pdf`) - via MuPDF
* **Comic Book Archive** (`.cbz`, `.zip` containing images) - via MuPDF native support
* **EPUB** (`.epub`) - via MuPDF native support

## Build Instructions
This project supports multiple platforms with a unified build system.

### Supported Platforms
- **TG5040** - Trimui Brick - Embedded Linux device (default)
- **macOS** - Desktop development and testing
- **Wii U** - Nintendo Wii U homebrew (requires devkitPro)
- **Linux** - Desktop Linux distributions (tested on Ubuntu 24.04)

### Quick Start
```bash
# Build for default platform (TG5040)
make

# Build for specific platform
make tg5040    # TG5040 embedded device (Trimui Brick)
make mac       # macOS
make wiiu      # Wii U (requires devkitPro environment)
make linux     # Linux desktop

# Export TG5040 distribution bundle
make export-tg5040    # Build and create complete TG5040 package

# List available platforms
make list-platforms

# Clean all build artifacts
make clean

# Show help
make help
```

### Platform-Specific Requirements

#### TG5040 (Embedded Linux)
* Cross-compilation toolchain for the target device
* Docker environment available (see `ports/tg5040/` for Docker setup)

#### macOS
* A C++17 compatible compiler (e.g., g++, clang++)
* `SDL2` and `SDL2_ttf` development libraries
* `MuPDF` development libraries  
* `pkg-config`

**Install dependencies using Homebrew:**
```bash
brew install sdl2 sdl2_ttf mupdf-tools pkg-config
```

#### Linux
* A C++17 compatible compiler (e.g., g++, clang++)
* `SDL2` and `SDL2_ttf` development libraries
* `MuPDF` development libraries and dependencies
* `pkg-config`

**Tested on Ubuntu 24.04. Other distributions may require different packages.**

**Install dependencies (Ubuntu/Debian):**
```bash
sudo apt install build-essential pkg-config libsdl2-dev libsdl2-ttf-dev libmupdf-dev libfreetype6-dev libharfbuzz-dev libjpeg-dev libopenjp2-7-dev libjbig2dec0-dev libgumbo-dev libmujs-dev
```

Or use the automated installer:
```bash
cd ports/linux && make install-deps
```

#### Wii U
* devkitPro toolchain with WUT (Wii U Toolchain)
* See `ports/wiiu/` for Wii U-specific build instructions

## Platform-Specific Features

### TG5040 Embedded Device
- **Advanced Power Management**: NextUI-compatible power button integration
  - Short press: Intelligent sleep with fallback to fake sleep mode
  - Fake sleep: Black screen with input blocking when hardware sleep unavailable
  - Automatic deep sleep attempts in background during fake sleep
  - Long press (2+ seconds): Safe shutdown
  - Smart error handling: No immediate errors, 30-second timeout before user notification
  - Event flushing on wake to prevent phantom button presses
- **Device-specific Input**: `/dev/input/event1` monitoring for power events
- **System Integration**: NextUI-compatible suspend/shutdown scripts
- **Bundle Export**: Complete distribution package creation
  - Self-contained bundle with all dependencies
  - Automated library bundling and RPATH setup
  - Includes utilities (jq, minui-list) and resources
  - Ready for device deployment via `make export-tg5040`

### macOS
- **Desktop Environment**: Standard desktop window management
- **Development Platform**: Full debugging and development tools available
- **Cross-platform Testing**: Verify functionality before deployment

### Linux
- **Desktop Environment**: Standard Linux desktop window management
- **Package Management**: Easy dependency installation via system package managers
- **Development Platform**: Full debugging and development tools available
- **Tested Platform**: Ubuntu 24.04 (other distributions may require additional setup)

### Wii U
- **Homebrew Environment**: Nintendo Wii U specific adaptations
- **Custom Input Handling**: Gamepad and touch screen support

## Usage
After building, run the executable from your project root, providing the path to a PDF or DjVu file as an argument:

```bash
./bin/sdl_reader_cli path/to/your_document.pdf
# or
./bin/sdl_reader_cli path/to/your_comic.cbz
# or
./bin/sdl_reader_cli path/to/your_book.epub
```

## TG5040 Deployment

For TG5040 (Trimui Brick) deployment, use the bundle export system:

### Creating Distribution Package
```bash
# Build and create complete distribution bundle
make export-tg5040

# Or build first, then export
make tg5040
make export-tg5040
```

### Bundle Contents
The exported bundle at `ports/tg5040/pak/` contains:
- **bin/**: Main executable and utilities (sdl_reader_cli, jq, minui-list)
- **lib/**: All shared library dependencies with proper RPATH setup
- **res/**: Font and resource files
- **launch.sh**: Main launcher script for the device

### Deployment to Device
1. Copy the entire `ports/tg5040/pak/` directory to your TG5040 device
2. Ensure the package has executable permissions
3. Run via the launch script or execute binaries directly

The bundle is completely self-contained and includes all necessary dependencies for the TG5040 platform.

## User Inputs
The SDL Reader supports the following keyboard and mouse inputs:

| Input                  | Action                                  |
| :--------------------- | :-------------------------------------- |
| **Keyboard** |                                         |
| `Q` or `Esc`           | Quit application                        |
| `Right Arrow`          | Scroll right                            |
| `Left Arrow`           | Scroll left                             |
| `Up Arrow`             | Scroll up                               |
| `Down Arrow`           | Scroll down                             |
| `Page Down`            | Go to next page                         |
| `Page Up`              | Go to previous page                     |
| `+` (Plus)             | Zoom in                                 |
| `-` (Minus)            | Zoom out                                |
| `Numpad +` / `Numpad -`| Zoom in/out (numpad support)           |
| `F`                    | Toggle Fullscreen                       |
| `G`                    | Jump to Page                            |
| `W`                    | Fit page to width                       |
| `R`                    | Reset page view                         |
| `Shift + R`            | Rotate page clockwise (90°)             |
| `H`                    | Toggle horizontal mirror                |
| `V`                    | Toggle vertical mirror                  |
| `[` (Left Bracket)     | Jump back 10 pages                     |
| `]` (Right Bracket)    | Jump forward 10 pages                  |
| **Mouse** |                                         |
| `Mouse Wheel Up`       | Scroll up                               |
| `Mouse Wheel Down`     | Scroll down                             |
| `Ctrl + Mouse Wheel Up`| Zoom in                                 |
| `Ctrl + Mouse Wheel Down`| Zoom out                              |
| `Left Click + Drag`    | Pan/Scroll                              |

## Project Structure
```
SDLReader-brick/
├── Makefile                      # Main multi-platform build system
├── src/                          # Shared source code
├── include/                      # Shared header files
├── cli/                          # Command-line interface
├── res/                          # Resources (fonts, etc.)
└── ports/                        # Platform-specific builds
    ├── tg5040/                   # TG5040 embedded device
    │   ├── Makefile              # TG5040 build configuration
    │   ├── Makefile.docker       # Docker environment management
    │   ├── docker-compose.yml    # Docker Compose setup
    │   ├── Dockerfile            # TG5040 toolchain image
    │   ├── export_bundle.sh      # Bundle export script
    │   ├── make_bundle2.sh       # Library dependency bundler
    │   ├── pak/                  # Distribution bundle (created by export)
    │   │   ├── bin/              # Executables (jq, minui-list, sdl_reader_cli)
    │   │   ├── lib/              # Shared libraries and dependencies
    │   │   ├── res/              # Resources (fonts, etc.)
    │   │   └── launch.sh         # Main launcher script
    │   ├── include/              # TG5040-specific headers
    │   │   └── power_handler.h   # Power management for TG5040
    │   └── src/                  # TG5040-specific source code
    │       └── power_handler.cpp # Power button handling implementation
    ├── mac/                      # macOS desktop
    │   └── Makefile              # macOS build configuration
    ├── linux/                    # Linux desktop
    │   ├── Makefile              # Linux build configuration
    │   └── README.md             # Linux-specific instructions
    └── wiiu/                     # Nintendo Wii U homebrew
        └── Makefile              # Wii U build configuration
```  

### Development Workflow
- **Cross-platform code**: Modify files in `src/`, `include/`, `cli/`
- **Platform-specific code**: Use conditional compilation (`#ifdef TG5040_PLATFORM`) or create port-specific implementations in `ports/{platform}/`
- **Power management**: TG5040 includes hardware-specific power button handling and suspend/resume functionality
- **Build system**: Each platform has its own Makefile in `ports/{platform}/`
- **Docker development**: Use `ports/tg5040/docker-compose.yml` for containerized development
- **TG5040 distribution**: Use `make export-tg5040` to create complete deployment package

## Architecture

For detailed information about the multi-platform design, conditional compilation strategies, and guidelines for adding new platforms, see [ARCHITECTURE.md](ARCHITECTURE.md).

Key architectural highlights:
- **Clean separation** between shared and platform-specific code
- **Conditional compilation** for platform-specific features
- **Port-specific implementations** for hardware integration (e.g., TG5040 power management)
- **Unified build system** with platform-specific Makefiles
- **Scalable design** for easy addition of new platforms

# Acknowledgements

This project was inspired by SDLBook.
