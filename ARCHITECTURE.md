# SDLReader Architecture

## Multi-Platform Design

SDLReader is designed as a cross-platform document reader with a clean separation between shared functionality and platform-specific features.

## Code Organization

### Shared Components (`src/`, `include/`, `cli/`)
Core functionality that works across all platforms:
- **Document handling**: PDF parsing and rendering via MuPDF
- **Rendering engine**: SDL2-based graphics and text rendering  
- **User interface**: Page navigation, zoom, scroll controls
- **Application logic**: Event handling, state management

### Platform-Specific Components (`ports/{platform}/`)
Platform-specific implementations and optimizations:
- **TG5040**: Embedded Linux with hardware power management
- **macOS**: Desktop development and testing environment
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
- **Wii U**: Uses console's built-in power management

## Build System Architecture

### Main Makefile (`Makefile`)
- Dispatches builds to platform-specific Makefiles
- Provides unified interface (`make tg5040`, `make mac`, etc.)
- Handles cross-platform cleaning and help

### Platform Makefiles (`ports/{platform}/Makefile`)
- Platform-specific compiler flags and dependencies
- Custom build rules for port-specific source files
- Library linking appropriate for each target

### Example: TG5040 Build Process
1. Sets `-DTG5040_PLATFORM` compiler flag
2. Includes `ports/tg5040/include` in header search path
3. Compiles `ports/tg5040/src/power_handler.cpp` as `tg5040_power_handler.o`
4. Links with platform-specific libraries

## Adding New Platforms

To add a new platform:

1. **Create port directory**: `ports/{new_platform}/`
2. **Add Makefile**: `ports/{new_platform}/Makefile`
3. **Define platform flag**: `-D{NEW_PLATFORM}_PLATFORM`
4. **Add platform-specific code** (if needed):
   - `ports/{new_platform}/include/` for headers
   - `ports/{new_platform}/src/` for implementations
5. **Update main Makefile**: Add new target
6. **Add conditional compilation**: Use `#ifdef {NEW_PLATFORM}_PLATFORM`

## Benefits of This Architecture

1. **Maintainability**: Platform code is isolated and focused
2. **Flexibility**: Easy to add/remove platform-specific features
3. **Clean Builds**: No unused code compiled for any platform
4. **Development**: Each platform can evolve independently
5. **Testing**: Cross-platform compatibility is maintained

## Development Workflow

1. **Shared features**: Modify files in `src/`, `include/`, `cli/`
2. **Platform features**: Add to appropriate `ports/{platform}/` directory
3. **Testing**: Build and test on each target platform
4. **Documentation**: Update platform-specific README files
