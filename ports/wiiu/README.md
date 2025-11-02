# Wii U Port - SDLReader for Nintendo Wii U

> ⚠️ **Warning:** This port has not been re-tested since the project transitioned from SDLReader to SDLReader-Brick; expect breakage until verification completes.

This directory contains the Wii U-specific build configuration for the SDLReader project.

SDLReader is a document reader that compiles and runs on macOS, TG5040, and Wii U. It uses MuPDF for loading PDF and comic book files.

## Key Features

- **Built-in CBR Support**: Now includes support for CBR (Comic Book RAR) files alongside CBZ/ZIP comic books
- **WebP Patch Applied**: WebP support patch applied to MuPDF (requires WebP libraries for full functionality)
- **Self-Contained MuPDF**: Uses port-specific MuPDF build with libarchive support
- **Wii U Optimized**: Static linking and console-specific optimizations
- **Nuklear UX (Desktop Parity)**: Shares the Nuklear file browser, font picker, and reading-style menu introduced on desktop/TG5040 (rendering still under active debugging)
- **Multiple Document Formats**: PDF, CBZ, CBR, ZIP, EPUB support

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
- Git (for automatic MuPDF download)
- libarchive support (for CBR files)
- **WebP support**: Limited by devkitPro library availability (patch applied but WebP libraries may not be available)

## Applied Patches

The Wii U make target applies the shared `webp-upstream-697749.patch` before compiling MuPDF. This keeps WebP image decoding aligned with the desktop builds even though devkitPro’s packaged libraries lag behind upstream.

## Fonts & Reading Styles

Copy custom `.ttf` or `.otf` fonts into the `fonts/` directory (either in the source tree or the deployed SD card layout). The Options → Font & Reading Style menu in the Nuklear overlay will surface those fonts automatically so you can switch typography at runtime once rendering is stabilized.

## Platform-Specific Features
The Wii U build:
- Excludes hardware power button monitoring (uses console's own power management)
- Static linking only (Wii U homebrew requirement)
- Uses devkitPro's harfbuzz and freetype libraries
- Includes Wii U-specific main wrapper (`main_wiiu.cpp`)
- **CBR Support**: Comic Book RAR files via libarchive integration
- **WebP Patch Applied**: WebP support patch applied to MuPDF (full WebP functionality depends on devkitPro WebP library availability)
- **Self-built MuPDF**: Automatically downloads and builds MuPDF with CBR support


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
