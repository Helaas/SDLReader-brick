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
TG_INCLUDE_DIR := $(BASE_PATH)/include
CXXFLAGS := -std=c++17 -Wall -Wextra -g -O3 -DNDEBUG -DTG5040_PLATFORM\
            -I$(WORKSPACE_PATH)/src -I$(WORKSPACE_PATH)/include -I$(TG_INCLUDE_DIR) -I$(BASE_PATH)/mupdf/include \
            -I$(BASE_PATH)/nuklear-tg5040 -I$(BASE_PATH)/nuklear-tg5040/demo/sdl_renderer \
            -I/usr/local/libarchive-minimal/include -D_REENTRANT -I/usr/include/SDL2

# Source directories
SRC_DIR = $(WORKSPACE_PATH)/src
CLI_DIR = $(WORKSPACE_PATH)/cli
TG_SRC_DIR = $(BASE_PATH)/src

# Output locations
BIN_DIR  := $(WORKSPACE_PATH)/bin
BUILD_DIR  := $(WORKSPACE_PATH)/build
TARGET   := $(BIN_DIR)/sdl_reader_cli

# Source files: Combine all .cpp files from src/ and cli/, excluding Linux-specific files
SRC_FILES = $(filter-out $(SRC_DIR)/power_handler.cpp, $(wildcard $(SRC_DIR)/*.cpp))
SRCS = $(SRC_FILES) $(wildcard $(CLI_DIR)/*.cpp)

# All object files (will be placed in the build directory)
# Transforms src/file.cpp into build/file.o and cli/file.cpp into build/file.o
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%,$(SRCS))) \
       $(patsubst $(CLI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(CLI_DIR)/%,$(SRCS))) \
       $(BUILD_DIR)/tg5040_power_handler.o

# Path to minimal libarchive static library
LIBARCHIVE_MINIMAL := /usr/local/libarchive-minimal/lib/libarchive.a

# Libraries (link order matters)
# Using static libarchive to avoid bundling extra dependencies
LIBS := -lSDL2_ttf -lSDL2 \
  -L$(BASE_PATH)/mupdf/build/release \
  -lmupdf -lmupdf-third \
  $(LIBARCHIVE_MINIMAL) \
  -ljpeg -lopenjp2 -lharfbuzz -ljbig2dec \
  -lfreetype -lz -llzma -lm -lpthread -ldl -lwebp -lwebpdemux -lgumbo -lstdc++fs

.PHONY: all clean export-bundle build-mupdf setup-mupdf clean-mupdf build-libarchive clean-libarchive setup-nuklear clean-nuklear check-libarchive

# Default goal - ensures minimal libarchive is built before anything else
all: setup-nuklear build-libarchive setup-mupdf build-mupdf $(TARGET)

# Helper target to verify minimal libarchive is built
check-libarchive:
	@if [ ! -f "$(LIBARCHIVE_MINIMAL)" ]; then \
		echo "ERROR: Minimal libarchive not found at $(LIBARCHIVE_MINIMAL)"; \
		echo "Run 'make build-libarchive' first or 'make all' to build everything"; \
		exit 1; \
	else \
		echo "✓ Minimal libarchive found at $(LIBARCHIVE_MINIMAL)"; \
	fi

# Setup Nuklear GUI library (header-only) if it doesn't exist
setup-nuklear:
	@cd $(BASE_PATH) && \
	if [ ! -d "nuklear-tg5040" ]; then \
		echo "Setting up Nuklear GUI library (v4.12.8) for SDL 2.0.9 support (TG5040)..."; \
		rm -rf nuklear-tg5040; \
		git clone --depth 1 --branch 4.12.8 https://github.com/Immediate-Mode-UI/Nuklear.git nuklear-tg5040; \
		echo "Nuklear repository cloned for TG5040"; \
	else \
		echo "Nuklear repository already present for TG5040"; \
	fi
	@cd $(BASE_PATH) && \
	if [ -d "nuklear-tg5040" ]; then \
		if [ ! -f "nuklear-tg5040/.tg5040_patched" ]; then \
			echo "Applying TG5040 SDL renderer compatibility patch to Nuklear..."; \
		(cd nuklear-tg5040 && (patch --batch -s -N -p1 < ../patches/nuklear_sdl_renderer_compat.patch || true)); \
		touch nuklear-tg5040/.tg5040_patched; \
		rm -f nuklear-tg5040/demo/sdl_renderer/nuklear_sdl_renderer.h.rej; \
		else \
			echo "Nuklear SDL renderer already patched for TG5040"; \
		fi; \
	fi

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

# Build minimal libarchive without ICU/XML dependencies
# This creates a custom static libarchive build that excludes:
# - ICU libraries (libicudata, libicui18n, libicuuc)
# - XML2 library support
# - Internationalization features
# This results in a much smaller bundle while maintaining CBR/CBZ functionality
build-libarchive: $(LIBARCHIVE_MINIMAL)

$(LIBARCHIVE_MINIMAL):
	@echo "Building minimal libarchive without ICU dependencies..."
	@if [ ! -f "$(LIBARCHIVE_MINIMAL)" ]; then \
		echo "Minimal libarchive not found, building from source..."; \
		cd $(BASE_PATH) && \
		if [ ! -d "libarchive-3.3.3" ]; then \
			echo "Downloading libarchive 3.3.3..."; \
			curl -L https://www.libarchive.org/downloads/libarchive-3.3.3.tar.gz | tar -xzf -; \
		fi && \
		cd libarchive-3.3.3 && \
		echo "Configuring minimal libarchive build..."; \
		./configure --prefix=/usr/local/libarchive-minimal \
			--disable-shared --enable-static \
			--without-xml2 --without-expat \
			--without-iconv --without-libiconv-prefix \
			--without-nettle --without-openssl \
			--disable-bsdtar --disable-bsdcat --disable-bsdcpio \
			--disable-xattr --disable-acl \
			--with-zlib --with-bz2lib --with-lzma && \
		echo "Compiling libarchive..." && \
		make -j$(nproc) && \
		echo "Installing to /usr/local/libarchive-minimal..." && \
		if command -v sudo >/dev/null 2>&1; then \
			sudo make install; \
		else \
			make install; \
		fi && \
		echo "Minimal libarchive built successfully at $(LIBARCHIVE_MINIMAL)"; \
	else \
		echo "Minimal libarchive already exists at $(LIBARCHIVE_MINIMAL)"; \
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

# Link final binary - depends on minimal libarchive being built
$(TARGET): $(OBJS) $(LIBARCHIVE_MINIMAL) | $(BIN_DIR)
	@echo "Linking sdl_reader_cli with minimal libarchive..."
	$(CXX) $(OBJS) $(LIBS) -o $@
	@patchelf --remove-rpath "$@" 2>/dev/null || true
	@patchelf --set-rpath '$$ORIGIN/../lib' "$@"
	@echo "Binary built successfully: $@"

# Compile source files into object files in the build directory
# This rule handles both src/ and cli/ cpp files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(CLI_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile TG5040-specific sources
$(BUILD_DIR)/tg5040_power_handler.o: $(TG_SRC_DIR)/power_handler.cpp | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)/*.o $(TARGET)
	@echo "Build artifacts cleaned"

# Clean MuPDF build
clean-mupdf:
	@echo "Cleaning MuPDF build..."
	@if [ -d "$(BASE_PATH)/mupdf" ]; then \
		cd $(BASE_PATH)/mupdf && $(MAKE) clean; \
	fi

# Clean libarchive build and installation
clean-libarchive:
	@echo "Cleaning minimal libarchive..."
	@if [ -d "$(BASE_PATH)/libarchive-3.3.3" ]; then \
		cd $(BASE_PATH)/libarchive-3.3.3 && make clean 2>/dev/null || true; \
	fi
	@echo "Removing libarchive installation..."
	@if command -v sudo >/dev/null 2>&1; then \
		sudo rm -rf /usr/local/libarchive-minimal; \
	else \
		rm -rf /usr/local/libarchive-minimal; \
	fi
	@echo "Minimal libarchive cleaned"

# Clean Nuklear checkout
clean-nuklear:
	@echo "Removing Nuklear checkout..."
	rm -rf $(BASE_PATH)/nuklear-tg5040

# Deep clean - removes everything including downloaded dependencies
clean-all: clean clean-mupdf clean-libarchive clean-nuklear
	@echo "Removing downloaded dependencies..."
	rm -rf $(BASE_PATH)/libarchive-3.3.3
	rm -rf $(BASE_PATH)/mupdf
	rm -f $(BASE_PATH)/webp-upstream-697749.patch
	rm -rf $(BASE_PATH)/pak
	@echo "Deep clean completed"
