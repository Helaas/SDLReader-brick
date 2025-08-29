# Wii U Port - SDLReader for Nintendo Wii U

This directory contains the Wii U-specific build configuration for the SDLReader project.

SDLReader is a PDF document reader that compiles and runs on macOS, TG5040, and Wii U. It uses MuPDF for loading PDF files.

## Quick Start

### From Project Root
```bash
# Build for Wii U (requires devkitPro environment)
make wiiu
```

### From This Directory  
```bash
cd ports/wiiu
make
```

## Requirements
- devkitPro toolchain with WUT (Wii U Toolchain)
- Custom MuPDF build for devkitPPC: [mupdf-devkitppc](https://github.com/hito16/mupdf-devkitppc)

## Platform-Specific Features
The Wii U build:
- Excludes hardware power button monitoring (uses console's own power management)
- Static linking only (Wii U homebrew requirement)  
- Uses devkitPro's harfbuzz and freetype libraries
- Includes Wii U-specific main wrapper (`main_wiiu.cpp`)


## Development Status

Current status of the Wii U port:
* ✅ [mupdf-devkitppc](https://github.com/hito16/mupdf-devkitppc) builds successfully
* ✅ Resolved multiple and undefined references
* ✅ Created main wrapper so Wii U code can call SDLReader code  
* ⚠️  Boots to blank screen - logging needed for debugging

## Detailed Build Instructions

Checkout the custom mupdf fork

```
gh repo clone hito16/mupdf-devkitppc
```

Start the devkitppc/WUT docker instance such that you see the following layout. (TBD - Add docker file)

```
/
  project/
    mupdf/
    SDLReader/
```

### Build the mupdf libraries.

start a compatible devkitpro Docker container

```
 docker build -t sdlreader-wiiu SDLReader/ports/wiiu
docker run -it --rm -v ${PWD}:/project --name sdlreader-wiiu  --hostname devkitppc  sdlreader-wiiu /bin/bash
```

```
cd SDLReader/ports/wiiu
./make_wiiu.sh

...
 AR build/release/libmupdf.a
..
 AR build/release/libmupdf-third.a
```

### Build the WiiU App


```
cd SDLReader/ports/wiiu
make 
```

## Build Notes

see make_wiiu.sh

WiiU homebrew only supports static builds.  
Mupdf and the WiiU both have versions of harfbuzz and freetype which generate multiple reference linker errors.

Here, we build against the devkitpro harfbuzz and freetype, supressing the mupdf versions.

```
HARFBUZZ_SRC= \
FREETYPE_SRC= \
XCFLAGS="$($DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-pkg-config --cflags harfbuzz freetype2)" \
XLIBS="$($DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-pkg-config --libs harfbuzz freetype2)" \

```


