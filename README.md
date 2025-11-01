# SDL Reader

SDL Reader is a lightweight, cross-platform document viewer built with SDL2 and MuPDF. It supports viewing PDF, CBZ/ZIP & CBR/RAR comic archives, EPUB books, and MOBI e-books with intuitive navigation, zooming, rotation, and mirroring features. Optimized for embedded devices like the TrimUI Brick (TG5040) running [NextUI](https://github.com/LoveRetro/NextUI). It also runs on desktop platforms including macOS, Linux, and is a work in progress as Wii U homebrew.

## Table of Contents
* [Features](#features)
* [Supported Document Types](#supported-document-types)
* [TrimUI Brick Control Scheme](#trimui-brick-control-scheme)
* [Build Instructions](#build-instructions)
* [Usage](#usage)
* [TG5040 Deployment](#tg5040-deployment)
* [User Inputs](#user-inputs)
* [Project Structure](#project-structure)
* [Architecture](#architecture)

## Features
* View PDF documents, comic book archives (CBZ/ZIP & CBR/RAR), EPUB books, and MOBI e-books.
* Built-in heads-up display with page, zoom, edge-turn, and error indicators.
* Integrated file browser (`--browse`) with controller support, persistent last directory, and TG5040-friendly layout, powered by Dear ImGui.
* Custom font picker with live preview, reading style themes, and MuPDF-backed CSS injection.
* On-screen number pad for page jumps when navigating with a controller.
* Automatic reading history tracking with resume-on-open for the last 50 documents.
* Page navigation (next/previous page) with **Smart Edge Navigation**: When zoomed ≥ 100% & at a page edge, hold D-pad for 300ms to flip pages with a progress indicator.
* Quick page jumping (±10 pages) and arbitrary page entry.
* Zoom in/out, fit-to-width, and full reset controls.
* Page rotation (90° increments) and mirroring (horizontal/vertical).
* Smooth scrolling within pages (if zoomed in or page is larger than the viewport).
* Toggle fullscreen mode (desktop platforms).
* TG5040-specific power button integration with fake sleep fallback.

## Supported Document Types
* **PDF** (`.pdf`)
* **Comic Book Archive** (`.cbz`, `.cbr`, `.rar`, `.zip` containing images)
  * **Enhanced WebP Support**: Supports WebP images within comic book archives
* **EPUB** (`.epub`)
* **MOBI** (`.mobi`)

## TrimUI Brick Control Scheme
![TrimUI Brick Controls](.github/resources/tg5040%20controls.png)

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
* **Limited external dependencies**: MuPDF & libarchive built automatically for optimal bundle size

#### macOS
* A C++17 compatible compiler (e.g., g++, clang++)
* `SDL2` and `SDL2_ttf` development libraries
* `libarchive` development library (for CBR support)
* `libwebp` development library (for WebP image support)
* `git` (for automatic MuPDF download)
* `pkg-config`

**Install dependencies using Homebrew:**
```bash
brew install sdl2 sdl2_ttf libarchive webp libarchive git pkg-config
```

**Note**: MuPDF is now built automatically from source with CBR and WebP support - no system MuPDF packages needed.

#### Linux
* A C++17 compatible compiler (e.g., g++, clang++)
* `SDL2` and `SDL2_ttf` development libraries
* `libarchive` development library (for CBR support)
* `libwebp` development library (for WebP image support)
* `git` (for automatic MuPDF download)
* `pkg-config`

**Tested on Ubuntu 24.04. Other distributions may require different packages.**

**Install dependencies (Ubuntu/Debian):**
```bash
sudo apt install build-essential pkg-config libsdl2-dev libsdl2-ttf-dev libfreetype6-dev libharfbuzz-dev libjpeg-dev libopenjp2-7-dev libjbig2dec0-dev libgumbo-dev libmujs-dev libarchive-dev libwebp-dev git
```

**Note**: MuPDF is now built automatically from source with CBR and WebP support - no system MuPDF packages needed.

Or use the automated installer:
```bash
cd ports/linux && make install-deps
```

#### Wii U
* devkitPro toolchain with WUT (Wii U Toolchain)
* See `ports/wiiu/` for Wii U-specific build instructions
* **Note**: WebP support may be limited due to devkitPro library availability

### Build-time Patches

All `make` targets download MuPDF 1.26.7 and apply `webp-upstream-697749.patch` (sourced from KOReader) to enable modern WebP decoding and fix upstream regressions. Platform exports that embed Dear ImGui may apply additional patches—see each port README for details. In particular, the TG5040 build also patches the ImGui SDL renderer backend for legacy SDL compatibility and swaps the physical A/B buttons to match the TrimUI Brick layout.

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
After building, you can either launch straight into a document or drop into the integrated browser:

```bash
# Open a specific file directly
./bin/sdl_reader_cli path/to/your_document.pdf
./bin/sdl_reader_cli path/to/your_comic.cbz
./bin/sdl_reader_cli path/to/your_book.epub
./bin/sdl_reader_cli path/to/your_ebook.mobi

# Launch the controller-friendly file browser (saves last directory)
./bin/sdl_reader_cli --browse
```

When using `--browse`, SDL Reader will remember the last directory you visited (stored in `config.json`) and automatically resume the last page you read for each document (stored in `reading_history.json`). Both files live in the reader state directory (`$SDL_READER_STATE_DIR`, defaulting to `$HOME`).

## Configuration

SDL Reader uses a `config.json` file (stored under `$SDL_READER_STATE_DIR`, defaulting to `$HOME/config.json`) for customizing font settings and display options.

### Setting up Configuration
1. Copy the example configuration file:
   ```bash
   cp config.json.example "$HOME/config.json"
   ```

2. Edit `config.json` to customize settings:
   ```json
   {
     "fontPath": "./fonts/JetBrainsMono-Bold.ttf",
     "fontName": "JetBrains Mono Bold",
     "fontSize": 16,
     "zoomStep": 10,
     "readingStyle": 0,
     "disableEdgeProgressBar": false,
     "lastBrowseDirectory": "/path/to/library"
   }
   ```

### Configuration Options
- **fontPath**: Path to the TTF/OTF font file to use for documents that support CSS (EPUB/MOBI)
- **fontName**: Display name for the font (shown in the font menu)
- **fontSize**: Default font size in points applied by the CSS generator
- **zoomStep**: Percentage increment for zoom operations and controller zoom buttons
- **readingStyle**: Numeric identifier for the active reading theme (see table below)
- **lastBrowseDirectory**: Directory the file browser should open by default when launched with `--browse`
- **disableEdgeProgressBar**: When `true`, panning at page edges changes pages instantly without the 300ms delay and progress bar. When `false` (default), the edge nudge progress bar is shown.
- **Environment override**: Set `SDL_READER_DEFAULT_DIR` to control the starting directory for the browser. If unset, the reader defaults to `$HOME`.

| `readingStyle` | Theme          | Background | Text Color |
| :------------- | :------------- | :--------- | :--------- |
| 0              | Document Default | Unchanged  | Unchanged  |
| 1              | Sepia          | #f4ecd8    | #5c4a3a    |
| 2              | Dark Mode      | #1e1e1e    | #d4d4d4    |
| 3              | High Contrast  | #ffffff    | #000000    |
| 4              | Paper Texture  | #faf8f3    | #2c2c2c    |
| 5              | Soft Gray      | #e8e8e8    | #333333    |
| 6              | Night Mode     | #0d0d0d    | #c9c9c9    |

All configuration values are saved automatically when you apply changes from the in-app font menu.

**Note**: Runtime `config.json` files are stored outside the repository (in the reader state directory) and are ignored by Git so you can personalize settings without affecting the repo.

**Adding new fonts:** Drop any `.ttf` or `.otf` files into the top-level `fonts/` directory (either on desktop or inside a TG5040 bundle). The Options → Font & Reading Style menu will automatically discover them, let you preview the typography, and persist your selection for EPUB/MOBI documents.

### Bundled Fonts & Licensing

The packaged builds include curated open fonts:

- Inter (Regular, Bold) — SIL Open Font License 1.1
- JetBrains Mono (Regular, Bold) — SIL Open Font License 1.1
- Noto Serif Condensed (Regular, Bold) — SIL Open Font License 1.1
- Roboto (Regular, Bold) — Apache License 2.0

Full license texts are provided in [`fonts/LICENSES.md`](fonts/LICENSES.md). Keep these notices with any redistributed bundle.

## Reading History

SDL Reader keeps a lightweight `reading_history.json` file in the reader state directory (`$SDL_READER_STATE_DIR`, defaulting to `$HOME/reading_history.json`). Every time you change pages, the current document path and page number are persisted so the next launch resumes automatically. The history remembers the most recent 50 documents. Delete the file if you want to reset all progress.

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
- **bin/**: `sdl_reader_cli` (legacy utilities such as `jq` and `minui-list` are preserved if you copy them in before exporting)
- **lib/**: All shared library dependencies with proper RPATH setup
- **fonts/**: All bundled font files ready for the runtime picker
- **res/**: Optional resources (e.g., documentation PDFs)
- **launch.sh**: Main launcher script that boots straight into the ImGui file browser
- **README.md / pak.json**: Copied for reference inside the bundle

### Deployment to Device
1. Copy the entire `ports/tg5040/pak/` directory to your TG5040 device
2. Ensure the package has executable permissions
3. Run via the launch script or execute binaries directly

The bundle is completely self-contained and includes all necessary dependencies for the TG5040 platform.

## User Inputs
The SDL Reader supports the following keyboard, mouse, and game controller inputs:

### TrimUI Brick Controls
| Button/Input                               | Action                                               |
| :----------------------------------------  | :-----------------------------------                 |
| **D-Pad**                                  |                                                      |
| `D-Pad Up/Down/Left/Right`                 | Scroll/Pan in direction                              |
| `D-Pad (at page edge)`                     | Hold for 300ms to turn page (with progress indicator)|
| **Shoulder Buttons**                       |                                                      |
| `L1 (Left Shoulder)`                       | Previous page                                        |
| `R1 (Right Shoulder)`                      | Next page                                            |
| `L2 (Left Trigger)`                        | Jump back 10 pages                                   |
| `R2 (Right Trigger)`                       | Jump forward 10 pages                                |
| **Face Buttons**                           |                                                      |
| `B`                                        | Fit page to width                                    |
| `A`                                        | Zoom out                                             |
| `X`                                        | Zoom in                                              |
| `Y`                                        | Rotate page clockwise (90°)                          |
| **Function Buttons**                       |                                                      |
| `F1`                                       | Reset page view                                      |
| `F2`                                       | Zoom to 200%                                         |
| **System Buttons**                         |                                                      |
| `Start`                                    | Toggle horizontal mirror                             |
| `Select`                                   | Toggle vertical mirror                               |
| `Menu (button 10)`                         | Toggle font & reading style menu                     |
| `Home (Guide)`                             | Quit application                                     |

### Keyboard Controls
| Input                                     | Action                                                |
| :---------------------------------------- | :---------------------------------------------------- |
| `Q` or `Esc`                              | Quit application                                      |
| `Right Arrow`                             | Scroll right                                          |
| `Left Arrow`                              | Scroll left                                           |
| `Up Arrow`                                | Scroll up                                             |
| `Down Arrow`                              | Scroll down                                           |
| `Page Down`                               | Go to next page                                       |
| `Page Up`                                 | Go to previous page                                   |
| `+` (Plus)                                | Zoom in                                               |
| `-` (Minus)                               | Zoom out                                              |
| `Numpad +` / `Numpad -`                   | Zoom in/out (numpad support)                          |
| `F`                                       | Toggle Fullscreen                                     |
| `G`                                       | Jump to Page                                          |
| `M`                                       | Toggle font & reading style menu                      |
| `W`                                       | Fit page to width                                     |
| `R`                                       | Reset page view                                       |
| `Shift + R`                               | Rotate page clockwise (90°)                           |
| `H`                                       | Toggle horizontal mirror                              |
| `V`                                       | Toggle vertical mirror                                |
| `[` (Left Bracket)                        | Jump back 10 pages                                    |
| `]` (Right Bracket)                       | Jump forward 10 pages                                 |

### Mouse Controls
| Input                                     | Action                                                |
| :---------------------------------------- | :---------------------------------------------------- |
| `Mouse Wheel Up`                          | Scroll up                                             |
| `Mouse Wheel Down`                        | Scroll down                                           |
| `Ctrl + Mouse Wheel Up`                   | Zoom in                                               |
| `Ctrl + Mouse Wheel Down`                 | Zoom out                                              |
| `Left Click + Drag`                       | Pan/Scroll                                            |

### SDL2 Game Controller Controls
| Button/Input                              | Action                                                |
| :---------------------------------------- | :---------------------------------------------------- |
| **D-Pad**                                 |                                                       |
| `D-Pad Up/Down/Left/Right`                | Scroll/Pan in direction                               |
| `D-Pad (zoomed ≥ 100% & at page edge)`    | Hold for 300ms to turn page (with progress indicator) |
| **Shoulder Buttons**                      |                                                       |
| `L1 (Left Shoulder)`                      | Previous page                                         |
| `R1 (Right Shoulder)`                     | Next page                                             |
| `L2 (Left Trigger)`                       | Jump back 10 pages                                    |
| `R2 (Right Trigger)`                      | Jump forward 10 pages                                 |
| **Face Buttons**                          |                                                       |
| `A`                                       | Fit page to width                                     |
| `B`                                       | Zoom out                                              |
| `X`                                       | Rotate page clockwise (90°)                           |
| `Y`                                       | Zoom in                                               |
| **System Buttons**                        |                                                       |
| `Start`                                   | Toggle font & reading style menu                      |
| `Back/Select`                             | Toggle vertical mirror                                |
| `Guide/Menu`                              | Quit application                                      |
| **Analog Sticks**                         |                                                       |
| `Left/Right Stick X-Axis`                 | Scroll horizontally                                   |
| `Left/Right Stick Y-Axis`                 | Scroll vertically                                     |

*Note: Controller button names may vary depending on your controller type (Xbox, PlayStation, etc.). The mappings above use SDL2's standardized button names.*

*Note 2: When using an Xbox controller, D-Pad Left & Right appear to trigger the Brick F1 & F2 functionality in addition to nudging.*

## Smart Edge Navigation

SDL Reader includes an intelligent edge navigation system for smooth page turning with game controllers:

### How It Works
- **Edge Detection**: When zoomed in ≥ 100%, using D-pad controls and reaching a page edge (left, right, top, or bottom), the system detects you're at the boundary
- **Hold to Turn**: Continue holding the D-pad direction for 300ms to initiate page turning (configurable - see below)
- **Visual Feedback**: A progress bar appears showing:
  - Direction of pending page change (e.g., "Next Page", "Previous Page")
  - Progress indicator that fills as you approach the 300ms threshold
  - Color transitions from yellow to green as the timer progresses
- **Immediate Cancellation**: Release the D-pad before 300ms to stay on the current page
- **Seamless Transition**: After page change, you appear at the appropriate edge of the new page for continuous navigation

### Instant Page Turns (No Delay)
You can disable the 300ms delay and progress bar for instant page turns:
- **Via Settings Menu**: Open Settings (Menu button on TG5040, `M` key on desktop) → Page Navigation section → Check "Disable Edge Progress Bar"
- **Via config.json**: Set `"disableEdgeProgressBar": true` in your configuration file
- When enabled, panning at page edges will change pages immediately without any delay or visual indicator

### When It Activates
- **Fully Zoomed Out**: When the page fits entirely within the window
- **At Scroll Limits**: When zoomed in and you've reached the maximum scroll position in any direction
- **D-pad Only**: This feature works with game controller D-pads, not keyboard arrow keys

### Benefits
- **Prevents Accidental Page Changes**: No more accidentally flipping pages when trying to scroll
- **Intuitive Control**: Natural feel for gaming device users
- **Visual Clarity**: Always know when a page change is about to happen

## Project Structure
```
SDLReader-brick/
├── Makefile                      # Main multi-platform build system
├── src/                          # Shared source code
├── include/                      # Shared header files
├── cli/                          # Command-line interface
├── fonts/                        # Font files (available to the font picker)
├── config.json.example           # Sample runtime configuration
├── reading_history.json          # Runtime cache (stored in the reader state directory)
└── ports/                        # Platform-specific builds
    ├── tg5040/                   # TG5040 embedded device
    │   ├── Makefile              # TG5040 build configuration
    │   ├── Makefile.docker       # Docker environment management
    │   ├── docker-compose.yml    # Docker Compose setup
    │   ├── Dockerfile            # TG5040 toolchain image
    │   ├── export_bundle.sh      # Bundle export script
    │   ├── make_bundle2.sh       # Library dependency bundler
  │   ├── pak/                  # Distribution bundle (created by export)
  │   │   ├── bin/              # Executables (sdl_reader_cli + optional utilities)
    │   │   ├── lib/              # Shared libraries and dependencies
    │   │   ├── fonts/            # Font files
    │   │   ├── res/              # Other resources (if any)
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
- <a href="https://github.com/hito16" target="_blank" rel="noopener noreferrer"><img src="https://github.com/hito16.png" alt="@hito16" width="18" height="18" style="border-radius:50%"> hito16</a>, for starting the original SDl Reader project.
- [rofl0r/SDLBook](https://github.com/rofl0r/SDLBook), for inspiring hito16.
- [LoveRetro/NextUI](https://github.com/LoveRetro/NextUI), for creating an excellent OS for the TrimUI Brick.
- <a href="https://github.com/josegonzalez" target="_blank" rel="noopener noreferrer"><img src="https://github.com/josegonzalez.png" alt="@josegonzalez" width="18" height="18" style="border-radius:50%"> josegonzalez</a>, for minui-list and countless other tools.
- [UncleJunVIP/nextui-pak-store](https://github.com/UncleJunVIP/nextui-pak-store) for the Pak Store
- [ocornut/imgui](https://github.com/ocornut/imgui), for the Dear ImGui UI framework powering the overlay, browser, and font menus.
- [koreader/koreader](https://github.com/koreader/koreader), for the MuPDF WebP Patch.
- [Claude.ai](https://claude.ai), for creating Sonnet 4. I’m not a C++ programmer, but Sonnet gave me a fighting chance at getting this done in a reasonable timeframe.

# License
In order to comply with the MuPDF license, this project is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0). See the [LICENSE](LICENSE) file for details.
