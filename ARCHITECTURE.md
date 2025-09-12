# SDLReader Architecture

## Supported Document Formats

SDLReader now provides comprehensive document format support across all platforms:

- **PDF**: Portable Document Format files
- **CBZ**: Comic Book ZIP archives  
- **CBR**: Comic Book RAR archives (NEW! - via libarchive integration)
- **ZIP**: ZIP archives containing images
- **RAR**: RAR archives containing images (NEW! - via libarchive integration)
- **EPUB**: Electronic book format
- **MOBI**: Kindle book format

### CBR Support Architecture

Comic Book RAR (CBR) support is implemented through:
- **MuPDF Integration**: Built-in CBR handler in MuPDF's document loading system
- **libarchive Dependency**: Automatic RAR archive extraction and reading
- **Unified Interface**: CBR files handled identically to CBZ files from the application perspective
- **Cross-Platform**: Available on all supported platforms through self-built MuPDF

## Multi-Platform Design

SDLReader is designed as a cross-platform document reader with a clean separation between shared functionality and platform-specific features.

### Supported Platforms
- **TG5040** - Trimui Brick embedded Linux device with custom power management
- **macOS** - Desktop development and testing environment  
- **Linux** - Desktop Linux distributions (tested on Ubuntu 24.04)
- **Wii U** - Nintendo homebrew with console-specific features

## Platform Characteristics

### TG5040 (Trimui Brick)
**Type**: Embedded Linux handheld device  
**Role**: Primary target platform with custom hardware integration  
**Key Features**:
- Advanced power button handling (`/dev/input/event1`)
- NextUI-compatible suspend/resume power management
- Fake sleep mode with automatic deep sleep attempts
- Smart error handling with 30-second timeout
- Battery conservation optimizations
- Hardware-specific input device monitoring
- PDF, CBZ, CBR, EPUB, and MOBI document support

**Build Characteristics**:
- Cross-compilation required
- Docker-based development environment
- Platform flag: `-DTG5040_PLATFORM`
- Self-built MuPDF with libarchive for CBR support
- NextUI system integration scripts

### macOS
**Type**: Desktop development platform  
**Role**: Cross-platform development and testing  
**Key Features**:
- Full development toolchain
- Homebrew package management
- Desktop window management
- Debugging and profiling tools

**Build Characteristics**:
- Native compilation
- Homebrew package management for dependencies
- Self-built MuPDF with libarchive for CBR support
- No platform-specific flags

### Linux
**Type**: Desktop Linux distributions  
**Role**: Development platform and alternative desktop target  
**Key Features**:
- System package manager integration
- Standard Linux desktop environment
- Development and debugging tools
- Tested on Ubuntu 24.04

**Build Characteristics**:
- Native compilation
- System package dependencies (no longer requires system MuPDF)
- Self-built MuPDF with libarchive for CBR support
- Automated dependency installation

### Wii U
**Type**: Nintendo homebrew console  
**Role**: Console gaming platform  
**Key Features**:
- Gamepad and touch screen input
- Console-specific UI adaptations
- Static linking requirements
- Custom main wrapper

**Build Characteristics**:
- Cross-compilation with devkitPro
- Static library linking only
- Self-built MuPDF with libarchive for CBR support
- Console-specific libraries

## Code Organization

### Shared Components (`src/`, `include/`, `cli/`)
Core functionality that works across all platforms:
- **Document handling**: PDF, CBZ/ZIP, CBR/RAR, EPUB, and MOBI support via MuPDF's native format support
- **Rendering engine**: SDL2-based graphics and text rendering
- **User interface**: Page navigation, zoom, scroll controls
- **Application logic**: Event handling, state management, unified document format support
- **Document types**: PDF documents, comic book archives (CBZ/ZIP/CBR/RAR), EPUB books, and MOBI e-books through single document interface

### Platform-Specific Components (`ports/{platform}/`)
Platform-specific implementations and optimizations:
- **TG5040**: Embedded Linux with hardware power management and Trimui Brick integration
- **macOS**: Desktop development and testing environment
- **Linux**: Desktop Linux with standard package management and development tools
- **Wii U**: Nintendo homebrew with console-specific features

## Conditional Compilation Strategy

The codebase uses `#ifdef` directives to include platform-specific code:

```cpp
#ifdef TG5040_PLATFORM
#include "power_handler.h"  // TG5040-specific power management
#endif
```

This approach allows:
- Clean builds on platforms that don't need specific features
- Platform-specific optimizations without code bloat
- Easy addition of new platforms

## Power Management Example

The power management system demonstrates the port-specific architecture:

### TG5040 Implementation
- **Location**: `ports/tg5040/include/power_handler.h`, `ports/tg5040/src/power_handler.cpp`
- **Features**: 
  - Hardware power button monitoring via Linux input device
  - NextUI-compatible suspend/resume with fallback strategies
  - Fake sleep mode: Black screen with input blocking when hardware sleep fails
  - Automatic deep sleep attempts every 2 seconds during fake sleep
  - Smart error handling: Only shows errors after 30 seconds of failed attempts
  - Event flushing on wake to prevent phantom button presses
  - Manual wake from fake sleep via power button
- **Integration**: Compiled only when `TG5040_PLATFORM` is defined
- **Callback System**: GUI integration for fake sleep mode and error display

### Other Platforms
- **macOS**: No power management needed (desktop environment)
- **Linux**: Uses standard desktop environment power management
- **Wii U**: Uses console's built-in power management

## MuPDF Build System Architecture

### Self-Contained MuPDF Strategy

SDLReader now uses a self-contained MuPDF build system instead of relying on system packages:

**Benefits**:
- **CBR Support**: Guaranteed libarchive integration for RAR archives
- **Version Control**: Consistent MuPDF 1.26.7 across all platforms  
- **Reduced Dependencies**: No system MuPDF packages required
- **Feature Control**: Custom build flags and optimizations
- **Cross-Platform**: Same MuPDF version and features everywhere

### Per-Port MuPDF Directories

Each platform maintains its own MuPDF build:
- **Location**: `ports/{platform}/mupdf/`
- **Version**: MuPDF 1.26.7 (automatically downloaded)
- **Configuration**: Platform-specific build settings
- **libarchive**: Integrated for CBR support on all platforms

### Build Process

1. **Automatic Download**: Git clones MuPDF 1.26.7 if directory doesn't exist
2. **Configuration**: Creates `user.make` with libarchive settings
3. **Platform-Specific Setup**: Configures paths and dependencies for each platform
4. **Compilation**: Builds static libraries (`libmupdf.a`, `libmupdf-third.a`)
5. **Preservation**: MuPDF directory preserved between builds for speed

### Platform-Specific MuPDF Configurations

#### macOS
- **libarchive Detection**: Automatic Homebrew path detection
- **PKG_CONFIG_PATH**: Dynamic libarchive.pc discovery
- **Dependencies**: Homebrew SDL2, libarchive

#### Linux  
- **libarchive Integration**: System package dependencies
- **pkg-config**: Standard library detection
- **Dependencies**: apt packages (libarchive-dev, etc.)

#### TG5040
- **Cross-Compilation**: Docker environment with proper toolchain
- **Static Linking**: Embedded Linux compatibility
- **Dependencies**: Bundled libraries in container

#### Wii U
- **devkitPro Integration**: Console-specific toolchain
- **Static Only**: Homebrew linking requirements  
- **Dependencies**: devkitPro portlibs

## Build System Architecture

### Main Makefile (`Makefile`)
- Dispatches builds to platform-specific Makefiles
- Provides unified interface (`make tg5040`, `make mac`, `make linux`, `make wiiu`)
- Handles cross-platform cleaning and help

### Platform Makefiles (`ports/{platform}/Makefile`)
- Platform-specific compiler flags and dependencies
- Custom build rules for port-specific source files  
- Library linking appropriate for each target
- Dependency management (Homebrew, apt, devkitPro, etc.)

### Example Build Processes

#### TG5040 Build
1. Sets `-DTG5040_PLATFORM` compiler flag
2. Includes `ports/tg5040/include` in header search path
3. Compiles `ports/tg5040/src/power_handler.cpp` as `tg5040_power_handler.o`
4. **MuPDF Setup**: Automatically downloads and builds MuPDF with libarchive
5. Links with embedded Linux libraries (SDL2, SDL2_image, custom MuPDF)
6. Sets up Docker cross-compilation environment
7. Configures NextUI-compatible system integration

#### Linux Build  
1. **MuPDF Setup**: Automatically downloads and builds MuPDF with libarchive
2. Uses native compiler toolchain
3. Resolves dependencies via system package manager
4. Links with standard Linux desktop libraries and custom MuPDF
5. Provides automated dependency installation

#### macOS Build
1. **MuPDF Setup**: Automatically downloads and builds MuPDF with Homebrew libarchive
2. Uses Homebrew for dependency management
3. Links with macOS-specific frameworks and custom MuPDF
4. Excludes Linux-specific source files
5. Uses dynamic libarchive.pc discovery

## Adding New Platforms

To add a new platform to SDLReader:

### 1. Create Platform Directory Structure
```bash
mkdir -p ports/{new_platform}
touch ports/{new_platform}/Makefile
touch ports/{new_platform}/README.md
```

### 2. Set Up Build Configuration
Create `ports/{new_platform}/Makefile` with:
- Platform-specific compiler flags
- Dependency resolution (pkg-config, custom paths, etc.)
- Library linking appropriate for the target
- Source file inclusion/exclusion rules

### 3. Define Platform Identification
- Add `-D{NEW_PLATFORM}_PLATFORM` compiler flag
- Use consistent naming (e.g., `NINTENDO_SWITCH_PLATFORM`)

### 4. Add Platform-Specific Code (if needed)
```bash
mkdir -p ports/{new_platform}/include
mkdir -p ports/{new_platform}/src
```

### 5. Update Main Build System
Add to main `Makefile`:
```makefile
{new_platform}:
	@echo "Building for {New Platform}..."
	$(MAKE) -f ports/{new_platform}/Makefile
```

### 6. Add Conditional Compilation
Use platform flags in shared code:
```cpp
#ifdef NEW_PLATFORM_PLATFORM
#include "platform_specific_header.h"
#endif
```

### 7. Update Documentation
- Add platform to README.md supported platforms list
- Create platform-specific README in `ports/{new_platform}/`
- Update this ARCHITECTURE.md file

### 8. Consider Platform Requirements
- **Desktop platforms**: Standard libraries, window management
- **Embedded platforms**: Cross-compilation, custom hardware integration  
- **Console platforms**: Static linking, custom input handling
- **Mobile platforms**: Touch input, app lifecycle management

## Benefits of This Architecture

1. **Maintainability**: Platform code is isolated and focused
2. **Flexibility**: Easy to add/remove platform-specific features
3. **Clean Builds**: No unused code compiled for any platform
4. **Development**: Each platform can evolve independently
5. **Testing**: Cross-platform compatibility is maintained
6. **Scalability**: New platforms can be added without affecting existing ones
7. **Specialization**: Each platform can optimize for its specific constraints
8. **Dependency Management**: Platform-specific dependency resolution strategies

## Development Workflow

1. **Shared features**: Modify files in `src/`, `include/`, `cli/`
2. **Platform features**: Add to appropriate `ports/{platform}/` directory
3. **Testing**: Build and test on each target platform
4. **Documentation**: Update platform-specific README files
