# SDLReader Architecture

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
- Custom power button handling (`/dev/input/event1`)
- Suspend/resume power management
- NextUI system integration
- Battery conservation optimizations
- Hardware-specific input device monitoring

**Build Characteristics**:
- Cross-compilation required
- Docker-based development environment
- Platform flag: `-DTG5040_PLATFORM`
- Custom libraries and system scripts

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
- pkg-config dependency resolution
- Standard desktop libraries
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
- apt/system package dependencies
- Standard Linux libraries
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
- Console-specific libraries
- Custom MuPDF build required

## Code Organization

### Shared Components (`src/`, `include/`, `cli/`)
Core functionality that works across all platforms:
- **Document handling**: PDF parsing and rendering via MuPDF
- **Rendering engine**: SDL2-based graphics and text rendering  
- **User interface**: Page navigation, zoom, scroll controls
- **Application logic**: Event handling, state management

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
- **Features**: Hardware power button monitoring, suspend/resume, wake detection
- **Integration**: Compiled only when `TG5040_PLATFORM` is defined

### Other Platforms
- **macOS**: No power management needed (desktop environment)
- **Linux**: Uses standard desktop environment power management
- **Wii U**: Uses console's built-in power management

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
4. Links with embedded Linux libraries
5. Sets up Docker cross-compilation environment

#### Linux Build  
1. Uses native compiler toolchain
2. Resolves dependencies via system package manager
3. Links with standard Linux desktop libraries
4. Provides automated dependency installation

#### macOS Build
1. Uses Homebrew for dependency management
2. Links with macOS-specific frameworks
3. Excludes Linux-specific source files
4. Uses pkg-config for library discovery

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
