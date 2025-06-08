# SDLReader PDF document reader for WIIU

SDLReader compiles and runs on MacOS.  It uses a library called mupdf for loading PDF files.

# Status

Largely notes to myself at this point porting this to WiiU.

* [mupdf-devkitppc](https://github.com/hito16/mupdf-devkitppc) builds
* resolved multiple and undefined references.
* created a main wrapper so the WiiU code can call the SLDReader code.
* boots to blank screen.   Need to add logging


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


