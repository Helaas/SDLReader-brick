# TG5050-specific Makefile
# This Makefile contains the build configuration specific to the TG5050 platform (TrimUI Smart Pro S)
# The TG5050 shares the same hardware interface as the TG5040, so power handler code is reused.

# Determine base path - if we're in ports/tg5050, use relative paths, otherwise use full paths
ifneq ($(wildcard ../../src),)
  # We're being called from ports/tg5050 directory
  BASE_PATH := .
  WORKSPACE_PATH := ../..
else
  # We're being called from workspace root
  BASE_PATH := ports/tg5050
  WORKSPACE_PATH := .
endif

# Shared TrimUI include/source from TG5040 (power handler, etc.)
TG5040_PORT_PATH := $(WORKSPACE_PATH)/ports/tg5040

# Toolchain configuration
CROSS_COMPILE ?=
ifndef PREFIX
  ifneq ($(wildcard /opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr/include/SDL2),)
    PREFIX := /opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc/usr
  else
    PREFIX := /usr
  endif
endif
SDL2_INCLUDE := $(PREFIX)/include/SDL2

# Compiler and flags
CXX      ?= $(if $(CROSS_COMPILE),$(CROSS_COMPILE)g++,g++)
TG_INCLUDE_DIR := $(TG5040_PORT_PATH)/include
CXXFLAGS = -std=c++17 -Wall -Wextra -g -O3 -DNDEBUG -DTRIMUI_PLATFORM -DPLATFORM_POWER_DEVICE_PATH='"/dev/input/event2"'\
            -I$(WORKSPACE_PATH)/src -I$(WORKSPACE_PATH)/include -I$(TG_INCLUDE_DIR) -I$(BASE_PATH)/mupdf/include \
            -I$(BASE_PATH)/nuklear-tg5050 -I$(BASE_PATH)/nuklear-tg5050/demo/sdl_renderer \
            -I/usr/local/libarchive-minimal/include -I$(WEBP_PREFIX)/include -D_REENTRANT -I$(SDL2_INCLUDE)

# Source directories
SRC_DIR = $(WORKSPACE_PATH)/src
CLI_DIR = $(WORKSPACE_PATH)/cli
TG_SRC_DIR = $(TG5040_PORT_PATH)/src

# Output locations
BIN_DIR  := $(WORKSPACE_PATH)/bin
BUILD_DIR  := $(WORKSPACE_PATH)/build/tg5050
TARGET   := $(BUILD_DIR)/sdl_reader_cli

# Source files: Combine all .cpp files from src/ and cli/, excluding Linux-specific files
SRC_FILES = $(filter-out $(SRC_DIR)/power_handler.cpp, $(wildcard $(SRC_DIR)/*.cpp))
SRCS = $(SRC_FILES) $(wildcard $(CLI_DIR)/*.cpp)

# All object files (will be placed in the build directory)
# Transforms src/file.cpp into build/file.o and cli/file.cpp into build/file.o
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%,$(SRCS))) \
       $(patsubst $(CLI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(CLI_DIR)/%,$(SRCS))) \
       $(BUILD_DIR)/tg5050_power_handler.o

# Path to minimal libarchive static library
LIBARCHIVE_MINIMAL := /usr/local/libarchive-minimal/lib/libarchive.a
# Libarchive version (RAR5 support requires >= 3.4; we use 3.7.2)
LIBARCHIVE_VERSION := 3.7.2
LIBARCHIVE_SRC_DIR := libarchive-$(LIBARCHIVE_VERSION)
LIBARCHIVE_TARBALL := libarchive-$(LIBARCHIVE_VERSION).tar.gz
LIBARCHIVE_URL := https://www.libarchive.org/downloads/$(LIBARCHIVE_TARBALL)

# WebP (static build for MuPDF)
WEBP_VERSION := 1.3.2
WEBP_SRC_DIR := libwebp-$(WEBP_VERSION)
WEBP_TARBALL := libwebp-$(WEBP_VERSION).tar.gz
WEBP_URL := https://storage.googleapis.com/downloads.webmproject.org/releases/webp/$(WEBP_TARBALL)
WEBP_PREFIX := $(abspath $(BASE_PATH)/webp-install)
WEBP_LIB := $(WEBP_PREFIX)/lib/libwebp.a
GUMBO_INCLUDE := $(abspath $(BASE_PATH)/mupdf/thirdparty/gumbo-parser/src)

# Libraries (link order matters)
# Using static libarchive to avoid bundling extra dependencies; MuPDF is built with bundled third-party libs.
LIBS := -L$(PREFIX)/lib -lSDL2_ttf -lSDL2 \
  -L$(BASE_PATH)/mupdf/build/release \
  -lmupdf -lmupdf-third \
  -L$(WEBP_PREFIX)/lib -lwebp -lwebpdemux \
  $(LIBARCHIVE_MINIMAL) \
  -lbz2 -llz4 -lzstd -llzma -lz -lm -lpthread -ldl -lstdc++fs

.PHONY: all clean export-bundle build-mupdf setup-mupdf clean-mupdf build-libarchive clean-libarchive build-webp clean-webp setup-nuklear clean-nuklear check-libarchive

# Default goal - ensures minimal libarchive and WebP are built before anything else
all: setup-nuklear build-libarchive build-webp setup-mupdf build-mupdf $(TARGET)

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
	if [ ! -d "nuklear-tg5050" ]; then \
		echo "Setting up Nuklear GUI library (v4.12.8) for TG5050..."; \
		rm -rf nuklear-tg5050; \
		git clone --depth 1 --branch 4.12.8 https://github.com/Immediate-Mode-UI/Nuklear.git nuklear-tg5050; \
		echo "Nuklear repository cloned for TG5050"; \
	else \
		echo "Nuklear repository already present for TG5050"; \
	fi

# Setup MuPDF repository if it doesn't exist
setup-mupdf:
	@if [ ! -d "$(BASE_PATH)/mupdf" ]; then \
		echo "Cloning MuPDF repository (shallow clone for faster setup)..."; \
		git clone --depth 1 --branch 1.26.7 https://github.com/ArtifexSoftware/mupdf.git $(BASE_PATH)/mupdf; \
		echo "Downloading and applying WebP patch for enhanced image format support..."; \
		if [ ! -f $(BASE_PATH)/webp-upstream-697749.patch ]; then \
			if command -v curl >/dev/null 2>&1; then \
				curl -o $(BASE_PATH)/webp-upstream-697749.patch \
					https://raw.githubusercontent.com/koreader/koreader-base/30ddfa05d67a8db1118326141b02bb002e2e11c4/thirdparty/mupdf/webp-upstream-697749.patch; \
			elif command -v wget >/dev/null 2>&1; then \
				wget -O $(BASE_PATH)/webp-upstream-697749.patch \
					https://raw.githubusercontent.com/koreader/koreader-base/30ddfa05d67a8db1118326141b02bb002e2e11c4/thirdparty/mupdf/webp-upstream-697749.patch; \
			else \
				echo "Error: neither curl nor wget found"; exit 1; \
			fi; \
		fi; \
		cd $(BASE_PATH)/mupdf && patch -p1 < ../webp-upstream-697749.patch; \
	fi
	@echo "Setting up MuPDF for CBR and WebP support..."; \
	echo "USE_LIBARCHIVE=yes" > $(BASE_PATH)/mupdf/user.make; \
	echo "HAVE_WEBP=yes" >> $(BASE_PATH)/mupdf/user.make; \
	echo "XCFLAGS += -DHAVE_LIBARCHIVE -I/usr/local/libarchive-minimal/include" >> $(BASE_PATH)/mupdf/user.make; \
	echo "XCFLAGS += -DHAVE_WEBP -I$(WEBP_PREFIX)/include" >> $(BASE_PATH)/mupdf/user.make; \
	echo "SYS_LIBARCHIVE_CFLAGS := -I/usr/local/libarchive-minimal/include" >> $(BASE_PATH)/mupdf/user.make; \
	echo "SYS_LIBARCHIVE_LIBS := -L/usr/local/libarchive-minimal/lib -larchive" >> $(BASE_PATH)/mupdf/user.make; \
	echo "SYS_WEBP_CFLAGS := -I$(WEBP_PREFIX)/include" >> $(BASE_PATH)/mupdf/user.make; \
	echo "SYS_WEBP_LIBS := -L$(WEBP_PREFIX)/lib -lwebp -lwebpdemux" >> $(BASE_PATH)/mupdf/user.make
	@echo "Initializing minimal required submodules..."; \
	cd $(BASE_PATH)/mupdf && git submodule update --init --depth 1 thirdparty/freetype thirdparty/harfbuzz thirdparty/jbig2dec thirdparty/libjpeg thirdparty/openjpeg thirdparty/zlib thirdparty/lcms2 thirdparty/gumbo-parser
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
		if [ ! -d "$(LIBARCHIVE_SRC_DIR)" ]; then \
			echo "Downloading libarchive $(LIBARCHIVE_VERSION)..."; \
			if command -v curl >/dev/null 2>&1; then \
				curl -L $(LIBARCHIVE_URL) | tar -xzf -; \
			else \
				wget -O - $(LIBARCHIVE_URL) | tar -xzf -; \
			fi; \
		fi && \
		cd $(LIBARCHIVE_SRC_DIR) && \
		echo "Configuring minimal libarchive build (includes RAR5 support)..."; \
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

# Build static libwebp (decoder + demux) for MuPDF
build-webp: $(WEBP_LIB)

$(WEBP_LIB):
	@echo "Building static libwebp $(WEBP_VERSION)..."
	@cd $(BASE_PATH) && \
	if [ ! -d "$(WEBP_SRC_DIR)" ]; then \
		echo "Downloading libwebp $(WEBP_VERSION)..."; \
		if command -v curl >/dev/null 2>&1; then \
			curl -L $(WEBP_URL) | tar -xzf -; \
		else \
			wget -O - $(WEBP_URL) | tar -xzf -; \
		fi; \
	fi && \
	cd $(WEBP_SRC_DIR) && \
	./configure --prefix=$(WEBP_PREFIX) --disable-shared --enable-static \
		--disable-png --disable-jpeg --disable-tiff --disable-gif \
		--enable-libwebpdecoder --disable-libwebpextras \
		CC="$(CROSS_COMPILE)gcc" CXX="$(CROSS_COMPILE)g++" && \
	$(MAKE) -j$$(nproc) && \
	$(MAKE) install

# Build MuPDF with custom libarchive support for CBR/CBZ files and WebP support
build-mupdf: build-libarchive build-webp
	@echo "Building MuPDF with custom libarchive support for CBR/CBZ files, WebP support, and HTML engine..."
	@cd $(BASE_PATH)/mupdf && $(MAKE) libs \
		CFLAGS="-std=c17 -D_GNU_SOURCE -DFZ_ENABLE_BARCODE=0 -DFZ_ENABLE_BROTLI=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_EXTRACT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_XPS=0 -DHAVE_LIBARCHIVE -DHAVE_WEBP -Iinclude -I$(WEBP_PREFIX)/include -I$(GUMBO_INCLUDE)" \
		CXXFLAGS="-std=c++17 -D_GNU_SOURCE -DFZ_ENABLE_BARCODE=0 -DFZ_ENABLE_BROTLI=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_EXTRACT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_XPS=0 -DHAVE_LIBARCHIVE -DHAVE_WEBP -Iinclude -I$(WEBP_PREFIX)/include -I$(GUMBO_INCLUDE)" \
		HAVE_LIBARCHIVE=yes \
		HAVE_WEBP=yes \
		mujs=no \
		tesseract=no \
		barcode=no \
		extract=no \
		svg=no \
		brotli=no \
		xps=no \
		USE_SYSTEM_LIBS=no \
		USE_SYSTEM_FREETYPE=no \
		USE_SYSTEM_HARFBUZZ=no \
		USE_SYSTEM_JBIG2DEC=no \
		USE_SYSTEM_LIBJPEG=no \
		USE_SYSTEM_OPENJPEG=no \
		USE_SYSTEM_ZLIB=no \
		USE_SYSTEM_BROTLI=no \
		USE_ZXINGCPP=no \
		USE_SYSTEM_LCMS2=no

# Export complete TG5050 bundle for distribution
export-bundle: $(TARGET)
	@echo "Exporting TG5050 bundle..."
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
	$(CXX) $(OBJS) $(LIBS) -Wl,-rpath,'$$ORIGIN/../lib' -o $@
	@echo "Binary built successfully: $@"

# Compile source files into object files in the build directory
# This rule handles both src/ and cli/ cpp files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(CLI_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile TG5050 power handler (shared from TG5040 port)
$(BUILD_DIR)/tg5050_power_handler.o: $(TG_SRC_DIR)/power_handler.cpp | $(BUILD_DIR)
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
	@if [ -d "$(BASE_PATH)/$(LIBARCHIVE_SRC_DIR)" ]; then \
		cd $(BASE_PATH)/$(LIBARCHIVE_SRC_DIR) && make clean 2>/dev/null || true; \
	fi
	@echo "Removing libarchive installation..."
	@if command -v sudo >/dev/null 2>&1; then \
		sudo rm -rf /usr/local/libarchive-minimal; \
	else \
		rm -rf /usr/local/libarchive-minimal; \
	fi
	@echo "Minimal libarchive cleaned"

# Clean WebP build
clean-webp:
	@echo "Cleaning libwebp..."
	rm -rf $(BASE_PATH)/$(WEBP_SRC_DIR) $(WEBP_PREFIX)

# Clean Nuklear checkout
clean-nuklear:
	@echo "Removing Nuklear checkout..."
	rm -rf $(BASE_PATH)/nuklear-tg5050

# Deep clean - removes everything including downloaded dependencies
clean-all: clean clean-mupdf clean-libarchive clean-webp clean-nuklear
	@echo "Removing downloaded dependencies..."
	rm -rf $(BASE_PATH)/$(LIBARCHIVE_SRC_DIR) $(BASE_PATH)/libarchive-3.3.3
	rm -rf $(BASE_PATH)/$(WEBP_SRC_DIR) $(WEBP_PREFIX)
	rm -rf $(BASE_PATH)/mupdf
	rm -f $(BASE_PATH)/webp-upstream-697749.patch
	rm -rf $(BASE_PATH)/pak
	@echo "Deep clean completed"
