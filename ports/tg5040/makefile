# TG5040-specific Makefile
# This Makefile contains the build configuration specific to the TG5040 platform

# Determine base path - if we're in ports/tg5040, use relative paths, otherwise use full paths
ifneq ($(wildcard ../../src),)
  # We're being called from ports/tg5040 directory
  BASE_PATH := .
  WORKSPACE_PATH := ../..
else
  # We're being called from workspace root
  BASE_PATH := ports/tg5040
  WORKSPACE_PATH := .
endif

# Compiler and flags
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -O3 -DNDEBUG -DTG5040_PLATFORM\
            -I$(WORKSPACE_PATH)/src -I$(WORKSPACE_PATH)/include -I$(BASE_PATH)/include -I$(BASE_PATH)/mupdf/include \
            -I$(WORKSPACE_PATH)/thirdparty/nuklear \
            -I/usr/local/libarchive-minimal/include -D_REENTRANT -I/usr/include/SDL2

# Source directories
SRC_DIR = $(WORKSPACE_PATH)/src
CLI_DIR = $(WORKSPACE_PATH)/cli

# Output locations
BIN_DIR  := $(WORKSPACE_PATH)/bin
BUILD_DIR  := $(WORKSPACE_PATH)/build
TARGET   := $(BIN_DIR)/sdl_reader_cli

# Source files: Combine all .cpp files from src/ and cli/, excluding Linux-specific files
SRC_FILES = $(filter-out $(SRC_DIR)/power_handler.cpp $(SRC_DIR)/gui_manager.cpp, $(wildcard $(SRC_DIR)/*.cpp))
SRCS = $(SRC_FILES) $(wildcard $(CLI_DIR)/*.cpp)

# Nuklear is header-only, no additional source files needed
# GUI functionality will be integrated directly into gui_manager.cpp

# All object files (will be placed in the build directory)
# Transforms src/file.cpp into build/file.o and cli/file.cpp into build/file.o
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%,$(SRCS))) \
       $(patsubst $(CLI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(CLI_DIR)/%,$(SRCS))) \
       $(BUILD_DIR)/tg5040_power_handler.o

# Libraries (link order matters) - No OpenGL needed for SDL Renderer backend
LIBS := -lSDL2_ttf -lSDL2 \
  -L$(BASE_PATH)/mupdf/build/release \
  -lmupdf -lmupdf-third \
  -L/usr/local/libarchive-minimal/lib -larchive \
  -ljpeg -lopenjp2 -lharfbuzz -ljbig2dec \
  -lfreetype -lz -llzma -lm -lpthread -ldl -lwebp -lwebpdemux -lgumbo -lstdc++fs

.PHONY: all clean export-bundle build-mupdf setup-mupdf clean-mupdf build-libarchive clean-libarchive setup-nuklear clean-nuklear

# Default goal
all: setup-mupdf setup-nuklear build-libarchive build-mupdf $(TARGET)


# Setup MuPDF repository if it doesn't exist
setup-mupdf:
	@if [ ! -d "$(BASE_PATH)/mupdf" ]; then \
		echo "Cloning MuPDF repository (shallow clone for faster setup)..."; \
		git clone --depth 1 --branch 1.26.7 https://github.com/ArtifexSoftware/mupdf.git $(BASE_PATH)/mupdf; \
		echo "Downloading and applying WebP patch for enhanced image format support..."; \
		if [ ! -f webp-upstream-697749.patch ]; then \
			curl -o webp-upstream-697749.patch \
				https://raw.githubusercontent.com/koreader/koreader-base/30ddfa05d67a8db1118326141b02bb002e2e11c4/thirdparty/mupdf/webp-upstream-697749.patch; \
		fi; \
		cd $(BASE_PATH)/mupdf && patch -p1 < ../webp-upstream-697749.patch; \
	fi
	@echo "Setting up MuPDF for CBR and WebP support..."; \
	echo "USE_LIBARCHIVE=yes" > $(BASE_PATH)/mupdf/user.make; \
	echo "HAVE_WEBP=yes" >> $(BASE_PATH)/mupdf/user.make; \
	echo "XCFLAGS += -DHAVE_LIBARCHIVE -I/usr/local/libarchive-minimal/include" >> $(BASE_PATH)/mupdf/user.make; \
	echo "XCFLAGS += -DHAVE_WEBP" >> $(BASE_PATH)/mupdf/user.make; \
	echo "SYS_LIBARCHIVE_CFLAGS := -I/usr/local/libarchive-minimal/include" >> $(BASE_PATH)/mupdf/user.make; \
	echo "SYS_LIBARCHIVE_LIBS := -L/usr/local/libarchive-minimal/lib -larchive" >> $(BASE_PATH)/mupdf/user.make; \
	echo "SYS_WEBP_LIBS := -lwebp -lwebpdemux" >> $(BASE_PATH)/mupdf/user.make
	@echo "Initializing minimal required submodules..."; \
	cd $(BASE_PATH)/mupdf && git submodule update --init --depth 1 thirdparty/freetype thirdparty/harfbuzz thirdparty/jbig2dec thirdparty/libjpeg thirdparty/openjpeg thirdparty/zlib thirdparty/lcms2
	@echo "Applying patches to disable barcode compilation when barcode=no..."; \
	cd $(BASE_PATH)/mupdf && sed -i 's/-std=c++20/-std=c++17/g' Makelists

# Setup Nuklear GUI library if it doesn't exist
setup-nuklear:
	@if [ ! -d "$(BASE_PATH)/nuklear" ]; then \
		echo "Cloning Nuklear GUI library (shallow clone for faster setup)..."; \
		git clone --depth 1 --branch 4.12.2 https://github.com/Immediate-Mode-UI/Nuklear.git $(BASE_PATH)/nuklear; \
	fi

# Build minimal libarchive without ICU/XML dependencies
# This creates a custom static libarchive build that excludes:
# - ICU libraries (libicudata, libicui18n, libicuuc)
# - XML2 library support
# - Internationalization features
# This results in a much smaller bundle while maintaining CBR/CBZ functionality
build-libarchive:
	@echo "Building minimal libarchive without ICU dependencies..."
	@if [ ! -d "$(BASE_PATH)/libarchive-3.3.3" ]; then \
		cd $(BASE_PATH) && \
		curl -s https://www.libarchive.org/downloads/libarchive-3.3.3.tar.gz | tar -xzf - && \
		cd libarchive-3.3.3 && \
		./configure --prefix=/usr/local/libarchive-minimal \
			--disable-shared --enable-static \
			--without-xml2 --without-expat \
			--without-iconv --without-libiconv-prefix \
			--without-nettle --without-openssl \
			--disable-bsdtar --disable-bsdcat --disable-bsdcpio \
			--disable-xattr --disable-acl \
			--with-zlib --with-bz2lib --with-lzma && \
		make -j$(nproc) && \
		make install; \
	fi

# Build MuPDF with custom libarchive support for CBR/CBZ files and WebP support
build-mupdf: build-libarchive
	@echo "Building MuPDF with custom libarchive support for CBR/CBZ files, WebP support, and HTML engine..."
	@cd $(BASE_PATH)/mupdf && $(MAKE) libs \
		CFLAGS="-std=c17 -D_GNU_SOURCE -DFZ_ENABLE_BARCODE=0 -DFZ_ENABLE_BROTLI=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_EXTRACT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_XPS=0 -DHAVE_LIBARCHIVE -DHAVE_WEBP -Iinclude" \
		CXXFLAGS="-std=c++17 -D_GNU_SOURCE -DFZ_ENABLE_BARCODE=0 -DFZ_ENABLE_BROTLI=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_EXTRACT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_XPS=0 -DHAVE_LIBARCHIVE -DHAVE_WEBP -Iinclude" \
		HAVE_LIBARCHIVE=yes \
		HAVE_WEBP=yes \
		mujs=no \
		tesseract=no \
		barcode=no \
		extract=no \
		svg=no \
		brotli=no \
		xps=no \
		USE_SYSTEM_LIBS=yes \
		USE_SYSTEM_FREETYPE=no \
		USE_SYSTEM_HARFBUZZ=no \
		USE_SYSTEM_JBIG2DEC=no \
		USE_SYSTEM_LIBJPEG=no \
		USE_SYSTEM_OPENJPEG=no \
		USE_SYSTEM_ZLIB=no \
		USE_SYSTEM_BROTLI=no \
		USE_ZXINGCPP=no \
		USE_SYSTEM_LCMS2=no

# Export complete TG5040 bundle for distribution
export-bundle: $(TARGET)
	@echo "Exporting TG5040 bundle..."
	@cd $(BASE_PATH) && ./export_bundle.sh

# Ensure bin/ exists before linking
$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# Rule to create the build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Link final binary
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(OBJS) $(LIBS) -o $@
	@patchelf --remove-rpath "$@" 2>/dev/null || true
	@patchelf --set-rpath '$$ORIGIN/../lib' "$@"

# Compile source files into object files in the build directory
# This rule handles both src/ and cli/ cpp files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(CLI_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Nuklear is header-only, no compilation rules needed

# Compile TG5040-specific power handler
$(BUILD_DIR)/tg5040_power_handler.o: $(BASE_PATH)/src/power_handler.cpp | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)/*.o $(BIN_DIR)/*
	rm -rf pak/lib/*
	rm -f pak/bin/sdl_reader_cli
	rm -f pak/README.md pak/pak.json
	rm -rf lib/
	rm -f webp-upstream-697749.patch

# Clean custom libarchive build
clean-libarchive:
	@echo "Removing custom libarchive build..."
	rm -rf $(BASE_PATH)/libarchive-3.3.3 /usr/local/libarchive-minimal

# Clean nuklear (remove downloaded repository)
clean-nuklear:
	@echo "Removing Nuklear directory (will be re-downloaded on next build)..."
	rm -rf $(BASE_PATH)/nuklear

# Clean MuPDF (use with caution - will require re-download and rebuild)
clean-mupdf:
	@echo "Removing MuPDF directory (will be re-downloaded on next build)..."
	rm -rf $(BASE_PATH)/mupdf
