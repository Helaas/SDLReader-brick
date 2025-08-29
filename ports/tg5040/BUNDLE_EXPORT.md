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

### 3. Moved Bundling Script
- **Old location**: `/make_bundle2.sh` (root directory)
- **New location**: `/ports/tg5040/make_bundle2.sh` (port-specific)

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
│   ├── jq                    # JSON processor utility
│   ├── minui-list           # MinUI list utility  
│   └── sdl_reader_cli       # Main PDF reader binary
├── lib/
│   ├── bin/
│   │   ├── run.sh           # Launcher script with LD_LIBRARY_PATH
│   │   └── sdl_reader_cli   # Main binary (copy)
│   └── lib/                 # All shared library dependencies
│       ├── libSDL2*.so*     # SDL2 libraries
│       ├── libmupdf*.so*    # MuPDF libraries (if applicable)
│       └── ...              # All other dependencies
├── res/
│   └── Roboto-Regular.ttf   # Font resources
└── launch.sh                # Main launcher script
```

## Benefits

1. **Port-Specific Organization**: Bundle creation is now specific to TG5040
2. **Self-Contained**: Bundle includes all dependencies and resources
3. **Automated**: One command creates complete distribution package
4. **Clean Structure**: No build artifacts in main project directory
5. **Maintainable**: Port-specific scripts can evolve independently

## Technical Details

### Library Bundling
- Uses `make_bundle2.sh` to analyze binary dependencies
- Copies all required shared libraries
- Sets up proper RPATH for relative library loading
- Strips debug symbols to reduce size

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
- Temporary library directory (`../../lib/`)

```bash
make -f ports/tg5040/Makefile clean
```
