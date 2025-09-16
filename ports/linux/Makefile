# Makefile for SDL Reader on Linux

# Determine base path - if we're in ports/linux, use relative paths, otherwise use full paths
ifneq ($(wildcard ../../src),)
  # We're being called from ports/linux directory
  BASE_PATH := .
  WORKSPACE_PATH := ../..
else
  # We're being called from workspace root
  BASE_PATH := ports/linux
  WORKSPACE_PATH := .
endif

# Compiler
CXX = g++

# Source directories
SRC_DIR = $(WORKSPACE_PATH)/src
CLI_DIR = $(WORKSPACE_PATH)/cli
INCLUDE_DIR = $(WORKSPACE_PATH)/include

# Local MuPDF Build Paths (using our custom build with libarchive support)
MUPDF_DIR = $(BASE_PATH)/mupdf
MUPDF_VERSION = 1.26.7
MUPDF_BUILD_PATH = $(BASE_PATH)/mupdf/build/release

# Build directory for all object files
BUILD_DIR = $(BASE_PATH)/build

# Final executable directory (project root)
BIN_DIR = $(WORKSPACE_PATH)/bin

# Name of the final executable
TARGET_NAME = sdl_reader_cli
TARGET = $(BIN_DIR)/$(TARGET_NAME)

# Source files: All .cpp files from src/ and cli/
SRC_FILES = $(wildcard $(SRC_DIR)/*.cpp)
SRCS = $(SRC_FILES) $(wildcard $(CLI_DIR)/*.cpp)

# Compiler flags
# -std=c++17: Use C++17 standard
# -Wall -Wextra: Enable common warnings
# -g: Include debugging information
# -I$(INCLUDE_DIR): Include path for project's own header files
# -I$(MUPDF_DIR)/include: Include path for our custom MuPDF headers with libarchive support
CXXFLAGS = -std=c++17 -Wall -Wextra -g -I$(INCLUDE_DIR) -I$(MUPDF_DIR)/include

# Include paths for SDL2 and SDL2_ttf
INC_PATHS = $(shell pkg-config --cflags sdl2 SDL2_ttf freetype2 harfbuzz)

# Library paths and libraries to link
# Using pkg-config for available libraries and manual linking for MuPDF dependencies
# Libraries
LIBS = -lmupdf -lmupdf-third -larchive $(shell pkg-config --libs sdl2 SDL2_ttf freetype2 harfbuzz libjpeg libopenjp2 libjbig2dec libgumbo mujs) -lwebp -lwebpdemux

# All object files (will be placed in the build directory)
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%,$(SRCS))) \
       $(patsubst $(CLI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(CLI_DIR)/%,$(SRCS)))

.PHONY: all clean build-mupdf install-deps help setup-mupdf clean-mupdf

# Default target
all: $(BIN_DIR) $(BUILD_DIR) setup-mupdf build-mupdf $(TARGET)

# Setup target to download and build MuPDF if needed
.PHONY: setup-mupdf
setup-mupdf:
	@if [ ! -d $(MUPDF_DIR) ]; then \
		echo "Downloading MuPDF $(MUPDF_VERSION)..."; \
		git clone --recursive --depth 1 --branch $(MUPDF_VERSION) https://github.com/ArtifexSoftware/mupdf.git $(MUPDF_DIR); \
		echo "Downloading and applying WebP patch..."; \
		if [ ! -f webp-upstream-697749.patch ]; then \
			curl -o webp-upstream-697749.patch \
				https://raw.githubusercontent.com/koreader/koreader-base/30ddfa05d67a8db1118326141b02bb002e2e11c4/thirdparty/mupdf/webp-upstream-697749.patch; \
		fi; \
		cd $(MUPDF_DIR) && patch -p1 < ../webp-upstream-697749.patch; \
	fi
	@echo "Configuring MuPDF for libarchive and WebP support..."
	@echo "HAVE_LIBARCHIVE = yes" > $(MUPDF_DIR)/user.make
	@echo "SYS_LIBARCHIVE_LIBS = -larchive" >> $(MUPDF_DIR)/user.make
	@echo "HAVE_WEBP = yes" >> $(MUPDF_DIR)/user.make
	@echo "SYS_WEBP_LIBS = -lwebp -lwebpdemux" >> $(MUPDF_DIR)/user.make
	@echo "USE_SYSTEM_LIBS = no" >> $(MUPDF_DIR)/user.make
	@if [ ! -f $(MUPDF_DIR)/build/release/libmupdf.a ]; then \
		echo "Building MuPDF..."; \
		$(MAKE) -C $(MUPDF_DIR) libs; \
	fi

# Build MuPDF with libarchive support
build-mupdf:
	@echo "Building MuPDF with libarchive support..."
	@cd mupdf && $(MAKE) libs HAVE_LIBARCHIVE=yes USE_SYSTEM_LIBS=no USE_SYSTEM_FREETYPE=no USE_SYSTEM_HARFBUZZ=no USE_SYSTEM_JBIG2DEC=no USE_SYSTEM_LIBJPEG=no USE_SYSTEM_OPENJPEG=no USE_SYSTEM_ZLIB=no USE_SYSTEM_BROTLI=no

# Rule to create the bin directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Rule to create the build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Link the executable
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(OBJS) -L$(MUPDF_BUILD_PATH) -lmupdf -lmupdf-third -larchive $(shell pkg-config --libs sdl2 SDL2_ttf freetype2 harfbuzz) -lz -lm -lpthread -o $@

# Compile source files into object files in the build directory
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags sdl2 SDL2_ttf freetype2 harfbuzz) -c $< -o $@

$(BUILD_DIR)/%.o: $(CLI_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags sdl2 SDL2_ttf freetype2 harfbuzz) -c $< -o $@

# Install dependencies (Ubuntu/Debian)
# Dependency installation targets for various distributions
.PHONY: install-deps install-deps-ubuntu install-deps-debian install-deps-fedora install-deps-arch
install-deps: install-deps-ubuntu

install-deps-ubuntu:
	sudo apt update
	sudo apt install build-essential pkg-config libsdl2-dev libsdl2-ttf-dev libfreetype6-dev libharfbuzz-dev libjpeg-dev libopenjp2-7-dev libjbig2dec0-dev libgumbo-dev libmujs-dev libarchive-dev libwebp-dev git

install-deps-debian: install-deps-ubuntu

install-deps-fedora:
	@echo "Installing dependencies for Fedora/RHEL..."
	sudo dnf install -y gcc-c++ pkg-config SDL2-devel SDL2_ttf-devel freetype-devel libarchive-devel libwebp-devel git

install-deps-arch:
	@echo "Installing dependencies for Arch Linux..."
	sudo pacman -S --needed base-devel pkg-config sdl2 sdl2_ttf freetype2 libarchive libwebp git

# Clean up compiled files and directories
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
	rm -f webp-upstream-697749.patch

# Clean MuPDF (use with caution - will require re-download and rebuild)
clean-mupdf:
	@echo "Removing MuPDF directory (will be re-downloaded on next build)..."
	rm -rf $(MUPDF_DIR)

# Help target
help:
	@echo "SDL Reader Linux Build System"
	@echo "Usage:"
	@echo "  make                    - Build the application"
	@echo "  make clean              - Clean build artifacts"
	@echo "  make install-deps       - Install dependencies (Ubuntu/Debian)"
	@echo "  make install-deps-fedora- Install dependencies (Fedora/RHEL)"
	@echo "  make install-deps-arch  - Install dependencies (Arch Linux)"
	@echo "  make help               - Show this help"
	@echo ""
	@echo "Required dependencies:"
	@echo "  - build-essential (gcc, g++, make)"
	@echo "  - pkg-config"
	@echo "  - libsdl2-dev"
	@echo "  - libsdl2-ttf-dev"
	@echo "  - libfreetype6-dev"
	@echo "  - libharfbuzz-dev"
	@echo "  - libjpeg-dev"
	@echo "  - libopenjp2-7-dev"
	@echo "  - libjbig2dec0-dev"
	@echo "  - libgumbo-dev"
	@echo "  - libmujs-dev"
	@echo "  - libarchive-dev"
	@echo "  - git"
	@echo ""
	@echo "Note: MuPDF is now built locally with CBR support instead of using system packages"
