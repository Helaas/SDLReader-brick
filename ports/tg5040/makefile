# TG5040-specific Makefile
# This Makefile contains the build configuration specific to the TG5040 platform

# Compiler and flags
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -O3 -DNDEBUG -DTG5040_PLATFORM\
            -Isrc -Iinclude -Iports/tg5040/include -Iports/tg5040/mupdf/include \
            -I/usr/local/libarchive-minimal/include -D_REENTRANT -I/usr/include/SDL2

# Output locations
BIN_DIR  := bin
BUILD_DIR  := build
TARGET   := $(BIN_DIR)/sdl_reader_cli

# Object files
OBJS := $(BUILD_DIR)/mupdf_document.o $(BUILD_DIR)/renderer.o $(BUILD_DIR)/text_renderer.o $(BUILD_DIR)/tg5040_power_handler.o $(BUILD_DIR)/app.o $(BUILD_DIR)/main.o

# Libraries (link order matters)
LIBS := -lSDL2_ttf -lSDL2 \
  -Lports/tg5040/mupdf/build/release \
  -lmupdf -lmupdf-third \
  -L/usr/local/libarchive-minimal/lib -larchive \
  -ljpeg -lopenjp2 -lharfbuzz -ljbig2dec \
  -lfreetype -lgumbo -lz -llzma -lm -lpthread -ldl

.PHONY: all clean export-bundle build-mupdf setup-mupdf clean-mupdf build-libarchive clean-libarchive

# Default goal
all: setup-mupdf build-libarchive build-mupdf $(TARGET)

# Setup MuPDF repository if it doesn't exist
setup-mupdf:
	@if [ ! -d "ports/tg5040/mupdf" ]; then \
		echo "Cloning MuPDF repository (shallow clone for faster setup)..."; \
		git clone --depth 1 --branch 1.26.7 https://github.com/ArtifexSoftware/mupdf.git ports/tg5040/mupdf; \
	fi
	@echo "Initializing minimal required submodules..."; \
	cd ports/tg5040/mupdf && git submodule update --init --depth 1 thirdparty/freetype thirdparty/harfbuzz thirdparty/jbig2dec thirdparty/libjpeg thirdparty/openjpeg thirdparty/zlib thirdparty/lcms2
	@echo "Applying patches to disable barcode compilation when barcode=no..."; \
	cd ports/tg5040/mupdf && sed -i 's/-std=c++20/-std=c++17/g' Makelists

# Build minimal libarchive without ICU/XML dependencies
# This creates a custom static libarchive build that excludes:
# - ICU libraries (libicudata, libicui18n, libicuuc) 
# - XML2 library support
# - Internationalization features
# This results in a much smaller bundle while maintaining CBR/CBZ functionality
build-libarchive:
	@echo "Building minimal libarchive without ICU dependencies..."
	@if [ ! -d "ports/tg5040/libarchive-3.3.3" ]; then \
		cd ports/tg5040 && \
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

# Build MuPDF with custom libarchive support for CBR/CBZ files  
build-mupdf: build-libarchive
	@echo "Installing development packages for CBR support and HTML parsing..."
	@apt-get update && apt-get install -y libgumbo-dev || echo "Warning: Could not install some dev packages"
	@echo "Building minimal MuPDF with custom libarchive support for CBR/CBZ files..."
	@cd ports/tg5040/mupdf && $(MAKE) libs \
		HAVE_LIBARCHIVE=yes \
		CFLAGS="-std=c17 -D_GNU_SOURCE -DFZ_ENABLE_BARCODE=0 -DFZ_ENABLE_BROTLI=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_EXTRACT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_XPS=0" \
		CXXFLAGS="-std=c++17 -D_GNU_SOURCE -DFZ_ENABLE_BARCODE=0 -DFZ_ENABLE_BROTLI=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_EXTRACT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_XPS=0" \
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
		CXXFLAGS="-std=c++17 -g -O3 -DFZ_ENABLE_BARCODE=0" \
		XCXXFLAGS="-std=c++17 -DFZ_ENABLE_BARCODE=0" \
		USE_SYSTEM_LCMS2=no

# Export complete TG5040 bundle for distribution
export-bundle: $(TARGET)
	@echo "Exporting TG5040 bundle..."
	@cd ports/tg5040 && ./export_bundle.sh

# Ensure bin/ exists before linking
$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# Link final binary
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(OBJS) $(LIBS) -o $@
	@patchelf --remove-rpath "$@" 2>/dev/null || true
	@patchelf --set-rpath '$$ORIGIN/../lib' "$@"

# Compile source to object files
$(BUILD_DIR)/%.o: src/%.cpp
	 @mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile sources from cli/
$(BUILD_DIR)/%.o: cli/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile TG5040-specific power handler
$(BUILD_DIR)/tg5040_power_handler.o: ports/tg5040/src/power_handler.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)/*.o $(BIN_DIR)/*
	rm -rf pak/ lib/

# Clean custom libarchive build
clean-libarchive:
	@echo "Removing custom libarchive build..."
	rm -rf ports/tg5040/libarchive-3.3.3 /usr/local/libarchive-minimal

# Clean MuPDF (use with caution - will require re-download and rebuild)
clean-mupdf:
	@echo "Removing MuPDF directory (will be re-downloaded on next build)..."
	rm -rf ports/tg5040/mupdf
