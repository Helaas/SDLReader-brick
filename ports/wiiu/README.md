# SDLReader PDF document reader for WIIU

SDLReader compiles and runs on MacOS.  It uses a library called mupdf for loading PDF files.

# Status

Largely notes to myself at this point porting this to WiiU.

* [mupdf-devkitppc](https://github.com/hito16/mupdf-devkitppc) builds
* resolved multiple and undefined references.
* created a main wrapper so the WiiU code can call the SLDReader code.

## Latest issue

* mupdf has its own harfbuzz and freetype under "thirdparty"
* Wiiu portlibs SDL_ttf has its on harfbuzz and freetype
* the working SDL app "SDLReader" builds on MacOS with generic SDL_ttf, but for WiiU, we will use the portlibs version. 

To get mupdf to compile with conpatible harfbuzz versions, I did the following
* instruct mupdf to NOT build harfbuzz and freetype
* add headers to address missing symbols from mupdf harfbuzz wrappers and from harfbuzz version incompatibility



```
root@72d6ab053451:/project/SDLReader/ports/wiiu# make
main_wiiu.cpp
app.cpp
pdf_document.cpp
renderer.cpp
text_renderer.cpp
wiiu_mupdf_hb_wrappers.c
wiiu_time_utils.c
ROMFS app.romfs.o
./
linking ... SDLReader-wiiu.elf
/opt/devkitpro/devkitPPC/bin/../lib/gcc/powerpc-eabi/14.2.0/../../../../powerpc-eabi/bin/ld: warning: /opt/devkitpro/devkitPPC/bin/../lib/gcc/powerpc-eabi/14.2.0/ecrtn.o: missing .note.GNU-stack section implies executable stack
/opt/devkitpro/devkitPPC/bin/../lib/gcc/powerpc-eabi/14.2.0/../../../../powerpc-eabi/bin/ld: NOTE: This behaviour is deprecated and will be removed in a future version of the linker
Failed to calculate offset for section .note.GNU-stack (21)
ERROR: calculateSectionOffsets failed.
make[1]: *** [/opt/devkitpro/wut/share/wut_rules:67: /project/SDLReader/ports/wiiu/SDLReader-wiiu.rpx] Error 255
make: *** [Makefile:152: build] Error 2
```

What made me think this would work?
* libmupdf.a and libmupdf-third.a compile, meaning their static build and link depdencies were satisfied. 

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


