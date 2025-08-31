# TG5040 Bundle Export System

This document describes the new TG5040 bundle export system that creates a complete distribution package.

## What Was Accomplished

### 1. Moved Bundle Structure to Port-Specific Location
- **Old location**: `/pak/` (root directory)
- **New location**: `/ports/tg5040/pak/` (port-specific)

### 2. Created Export Script
- **Script**: `ports/tg5040/export_bundle.sh`
- **Purpose**: Automated creation of complete TG5040 distribution bundle
- **Features**: 
  - Copies existing pak/bin utilities (jq, minui-list)
  - Copies main binary (sdl_reader_cli) 
  - Generates library dependencies using make_bundle2.sh
  - Copies resources (fonts, etc.)
  - Creates complete self-contained package
  - Calls  `ports/tg5040/make_bundle.sh`

### 3. Updated Bundling Script
- **Script**: `ports/tg5040/make_bundle.sh`
- **Purpose**: Generates library dependencies and sets up proper bundle structure
- **Features**:
  - Analyzes binary dependencies using `ldd`
  - Copies required shared libraries with exclusion filters
  - Sets up proper RPATH for relative library loading
  - Handles both regular bundle structure and direct lib directory output
  - Strips debug symbols to reduce bundle size

### 4. Added Make Targets
- **TG5040 Makefile**: `make export-bundle`
- **Main Makefile**: `make export-tg5040`

## Usage

### Quick Export
```bash
# From project root
make export-tg5040
```

### Manual Export  
```bash
# From ports/tg5040 directory
./export_bundle.sh
```

### Export After Build
```bash
# Build and export in one command
make tg5040 && make export-tg5040
```

## Bundle Structure

The exported bundle in `ports/tg5040/pak/` contains:

```
pak/
├── bin/
│   ├── jq                    # JSON processor utility (preserved)
│   ├── minui-list           # MinUI list utility (preserved)
│   └── sdl_reader_cli       # Main document reader binary (PDF, CBZ, ZIP)
├── lib/                     # All shared library dependencies
│   ├── libSDL2*.so*         # SDL2 libraries
│   ├── libmupdf*.so*        # MuPDF libraries
│   └── ...                  # All other dependencies
├── res/
│   └── Roboto-Regular.ttf   # Font resources
└── launch.sh                # Main launcher script (preserved)
```

## Benefits

1. **Port-Specific Organization**: Bundle creation is now specific to TG5040
2. **Self-Contained**: Bundle includes all dependencies and resources
3. **Automated**: One command creates complete distribution package
4. **Clean Structure**: No build artifacts in main project directory
5. **Maintainable**: Port-specific scripts can evolve independently

## Technical Details

### Library Bundling
- Uses `make_bundle.sh` to analyze binary dependencies with `ldd`
- Copies all required shared libraries with smart exclusion filters
- Excludes system libraries that must use device versions (glibc, SDL2, etc.)
- Sets up proper RPATH for relative library loading using `patchelf`
- Strips debug symbols to reduce bundle size

### File Preservation
- Preserves important existing files during bundle creation:
  - `bin/jq` - JSON processor utility
  - `bin/minui-list` - MinUI list utility
  - `launch.sh` - Main launcher script
- Uses temporary backup during clean/rebuild cycle

### Binary Collection
- Copies utilities from existing pak/bin
- Copies main sdl_reader_cli binary
- Ensures all binaries are executable

### Resource Management  
- Copies all resources needed at runtime
- Maintains directory structure for proper resource loading

## Clean Up

The TG5040 Makefile clean target now also removes:
- Generated bundle (`pak/`)
- Temporary library directory (`lib/`)

```bash
make -f ports/tg5040/Makefile clean
```
