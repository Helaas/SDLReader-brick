# SDL Reader

A minimal document reader built using SDL2 and  MuPDF supporting PDF 

## Table of Contents
* [Features](#features)
* [Supported Document Types](#supported-document-types)
* [Build Instructions](#build-instructions)
* [Usage](#usage)
* [User Inputs](#user-inputs)
* [Project Structure](#project-structure)

## Features
* View PDF documents.
* Page navigation (next/previous page).
* Zoom in/out.
* Scroll within pages (if zoomed in or page is larger than window).
* Toggle fullscreen mode.
* Basic UI overlay showing current page and zoom level.
* Planned: CBZ, EPUB (supported by mupdf)

## Supported Document Types
* **PDF** (`.pdf`)

## Build Instructions
This project supports multiple platforms with a unified build system.

### Supported Platforms
- **TG5040** - Embedded Linux device (default)
- **macOS** - Desktop development and testing
- **Wii U** - Nintendo Wii U homebrew (requires devkitPro)

### Quick Start
```bash
# Build for default platform (TG5040)
make

# Build for specific platform
make tg5040    # TG5040 embedded device
make mac       # macOS
make wiiu      # Wii U (requires devkitPro environment)

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

#### Wii U
* devkitPro toolchain with WUT (Wii U Toolchain)
* See `ports/wiiu/` for Wii U-specific build instructions

## Platform-Specific Features

### TG5040 Embedded Device
- **Power Management**: Hardware power button integration
  - Short press: Suspend device to save battery
  - Long press (2+ seconds): Safe shutdown
  - Wake detection with grace period handling
  - Error feedback through GUI notifications
- **Device-specific Input**: `/dev/input/event1` monitoring for power events
- **System Integration**: NextUI-compatible suspend/shutdown scripts

### macOS
- **Desktop Environment**: Standard desktop window management
- **Development Platform**: Full debugging and development tools available
- **Cross-platform Testing**: Verify functionality before deployment

### Wii U
- **Homebrew Environment**: Nintendo Wii U specific adaptations
- **Custom Input Handling**: Gamepad and touch screen support

## Usage
After building, run the executable from your project root, providing the path to a PDF or DjVu file as an argument:

```bash
./bin/sdl_reader_cli path/to/your_document.pdf
```

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
| `=` (Equals)           | Zoom in                                 |
| `-` (Minus)            | Zoom out                                |
| `F`                    | Toggle Fullscreen                       |
| `G`                    | Jump to Page (Not Implemented)          |
| **Mouse** |                                         |
| `Mouse Wheel Up`       | Scroll up                               |
| `Mouse Wheel Down`     | Scroll down                             |
| `Ctrl + Mouse Wheel Up`| Zoom in                                 |
| `Ctrl + Mouse Wheel Down`| Zoom out                              |
| `Left Click + Drag`    | Pan/Scroll                              |

## Project Structure
```
SDLReader-brick/
├── Makefile                    # Main multi-platform build system
├── src/                        # Shared source code
├── include/                    # Shared header files
├── cli/                        # Command-line interface
├── res/                        # Resources (fonts, etc.)
└── ports/                      # Platform-specific builds
    ├── tg5040/                 # TG5040 embedded device
    │   ├── Makefile            # TG5040 build configuration
    │   ├── Makefile.docker     # Docker environment management
    │   ├── docker-compose.yml  # Docker Compose setup
    │   ├── Dockerfile          # TG5040 toolchain image
    │   ├── include/            # TG5040-specific headers
    │   │   └── power_handler.h # Power management for TG5040
    │   └── src/                # TG5040-specific source code
    │       └── power_handler.cpp # Power button handling implementation
    ├── mac/                    # macOS desktop
    │   └── Makefile            # macOS build configuration
    └── wiiu/                   # Nintendo Wii U homebrew
        └── Makefile            # Wii U build configuration
```

### Development Workflow
- **Cross-platform code**: Modify files in `src/`, `include/`, `cli/`
- **Platform-specific code**: Use conditional compilation (`#ifdef TG5040_PLATFORM`) or create port-specific implementations in `ports/{platform}/`
- **Power management**: TG5040 includes hardware-specific power button handling and suspend/resume functionality
- **Build system**: Each platform has its own Makefile in `ports/{platform}/`
- **Docker development**: Use `ports/tg5040/docker-compose.yml` for containerized development

# Acknowledgements

This project was inspired by SDLBook.
