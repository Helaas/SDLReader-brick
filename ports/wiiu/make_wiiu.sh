#!/bin/bash

cd /project/mupdf-devkitppc

pwd


worked_but_big(){
make clean
make \
CC=$DEVKITPRO/devkitPPC/bin/powerpc-eabi-gcc \
CXX=$DEVKITPRO/devkitPPC/bin/powerpc-eabi-g++ \
AR="$DEVKITPRO/devkitPPC/bin/powerpc-eabi-ar" \
LD="$DEVKITPRO/devkitPPC/bin/powerpc-eabi-ld" \
CFLAGS="-I./include -O2 -ffunction-sections -fdata-sections -DESPRESSO -mcpu=750 -meabi -mhard-float -D__WIIU__ -D__WUT__ -DBIGENDIAN -DFZ_ENABLE_BARCODE=0 -D_GNU_SOURCE -DCMS_NO_PTHREADS" \
CXXFLAGS="-I./include -O2 -ffunction-sections -fdata-sections -DESPRESSO -mcpu=750 -meabi -mhard-float -D__WIIU__ -D__WUT__ -DBIGENDIAN -DFZ_ENABLE_BARCODE=0 -D_GNU_SOURCE -DCMS_NO_PTHREADS" \
HAVE_X11=no \
HAVE_GL=no \
HAVE_GLUT=no \
HAVE_CURL=no \
HAVE_OBJCOPY=no \
HAVE_HARFBUZZ=yes \
html=yes \
shared=no \
threading=no \
HARFBUZZ_SRC= \
FREETYPE_SRC= \
XCFLAGS="$($DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-pkg-config --cflags harfbuzz freetype2)" \
XLIBS="$($DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-pkg-config --libs harfbuzz freetype2)" \
build/release/libmupdf.a \
build/release/libmupdf-third.a

}

worked_but_big
