#!/bin/bash

cd /project/mupdf

pwd

remove_conflicting_thirdparty(){
# must set HAVE_CBZ=no
if [ -d "thirdparty/harfbuzz" ]; then
    echo "Backing up thirdparty/harfbuzz to thirdparty/harfbuzz_backup.zip..."
    zip -r thirdparty/harfbuzz_backup.zip thirdparty/harfbuzz
    echo "Removing original thirdparty/harfbuzz directory..."
    rm -rf thirdparty/harfbuzz
    echo "Backup complete. Original directory removed."
else
    echo "thirdparty/harfbuzz does not exist, skipping backup and removal."
fi
}

normal_make() {
# Define CFLAGS and CXXFLAGS as shell variables first for proper quoting
COMMON_CFLAGS="-I./include -O2 -ffunction-sections -fdata-sections -DESPRESSO -mcpu=750 -meabi -mhard-float -D__WIIU__ -D__WUT__ -DBIGENDIAN -DFZ_ENABLE_BARCODE=0 -DCMS_NO_PTHREADS -DTOFU "
COMMON_CXXFLAGS="-I./include -O2 -ffunction-sections -fdata-sections -DESPRESSO -mcpu=750 -meabi -mhard-float -D__WIIU__ -D__WUT__ -DBIGENDIAN -DFZ_ENABLE_BARCODE=0  -DCMS_NO_PTHREADS -DTOFU "

# --- NEW LINE ADDED HERE TO REMOVE HARFBUZZ SOURCE DIRECTORY ---
remove_conflicting_thirdparty

#make clean
make \
CC="$DEVKITPRO/devkitPPC/bin/powerpc-eabi-gcc" \
CXX="$DEVKITPRO/devkitPPC/bin/powerpc-eabi-g++" \
AR="$DEVKITPRO/devkitPPC/bin/powerpc-eabi-ar" \
CFLAGS="$COMMON_CFLAGS" \
CXXFLAGS="$COMMON_CXXFLAGS" \
HAVE_GL=no \
HAVE_GLUT=no \
HAVE_CURL=no \
HAVE_CBZ=no \
HAVE_EPUB=no \
HAVE_HARFBUZZ=no \
HAVE_HTML=no \
HAVE_JS=no \
HAVE_LCMS2=no \
HAVE_OBJCOPY=no \
HAVE_SVG=no \
HAVE_TEXT_OUTPUT=no \
HAVE_X11=no \
HAVE_XPS=no \
NO_OCRLIB=yes \
mujs=no \
xps=no \
svg=no \
html=no \
FONT_BIN= \
build/release/libmupdf.a \
build/release/libmupdf-third.a
#FONT_GEN= \

}


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
HAVE_OBJCOPY=yes \
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

#normal_make
worked_but_big
