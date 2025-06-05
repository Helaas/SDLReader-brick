# SDLReader PDF document reader for WIIU

# Status

Largely notes to myself at this point.

* mupdf builds, 
* resolved multiple and undefined references.
* need to create a main wrapper so the WiiU code can call the SLDReader code.

## Building

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

```
cd SDLReader/ports/wiiu
./make_wiiu.sh

...
 AR build/release/libmupdf.a
..
 AR build/release/libmupdf-third.a
```

### Build the WiiU App

TBD - haven't written it yet :)

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

Later we fill in the missing undefined references with custom code.

