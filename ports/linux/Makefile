# Makefile for SDL Reader on Linux
#
# This Makefile automatically handles dependencies:
# - Checks for required system libraries (SDL2, libarchive, webp, etc.)
# - Downloads and sets up Nuklear and MuPDF if needed
# - Provides targets for dependency management
#
# Usage:
#   make install-deps  # Install system dependencies
#   make              # Build the project
#   make help         # Show available targets

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

# Nuklear directory (using tg5040's nuklear port)
NUKLEAR_DIR = $(WORKSPACE_PATH)/ports/tg5040/nuklear

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

# Source files: Combine all .cpp files from src/ and cli/, excluding Linux-specific files
SRC_FILES = $(filter-out $(SRC_DIR)/power_handler.cpp, $(wildcard $(SRC_DIR)/*.cpp))
SRCS = $(SRC_FILES) $(wildcard $(CLI_DIR)/*.cpp)

# Nuklear SDL renderer implementation
NUKLEAR_IMPL = $(NUKLEAR_DIR)/demo/sdl_renderer/nuklear_sdl_renderer.c

# Add Nuklear implementation to compilation
SRCS += $(NUKLEAR_IMPL)

# Compiler flags
# -std=c++17: Use C++17 standard
# -Wall -Wextra: Enable common warnings
# -g: Include debugging information
# -I$(INCLUDE_DIR): Include path for project's own header files
# -I$(MUPDF_DIR)/include: Include path for our custom MuPDF headers with libarchive support
# -I$(NUKLEAR_DIR): Include path for Nuklear headers
# -I$(NUKLEAR_DIR)/demo/sdl_renderer: Include path for Nuklear SDL renderer backend
CXXFLAGS = -std=c++17 -Wall -Wextra -g -I$(INCLUDE_DIR) -I$(MUPDF_DIR)/include -I$(NUKLEAR_DIR) -I$(NUKLEAR_DIR)/demo/sdl_renderer

# Include paths for SDL2 and SDL2_ttf
INC_PATHS = $(shell pkg-config --cflags sdl2 SDL2_ttf freetype2 harfbuzz)

# Library paths and libraries to link
# Library order matters for static libraries - dependencies should come after the libraries that use them
# MuPDF libraries must come before their dependencies (libarchive, libwebp)
LIBS = -L$(MUPDF_BUILD_PATH) -lmupdf -lmupdf-third \
       -larchive -lwebp -lwebpdemux \
       $(shell pkg-config --libs sdl2 SDL2_ttf freetype2 harfbuzz) \
       -lz -lm -lpthread

# All object files (will be placed in the build directory)
# Transforms src/file.cpp into build/file.o and cli/file.cpp into build/file.o
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%,$(SRCS))) \
       $(patsubst $(CLI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(CLI_DIR)/%,$(SRCS))) \
       $(BUILD_DIR)/nuklear_sdl_renderer.o

.PHONY: all clean build-mupdf install-deps help setup-mupdf clean-mupdf clean-all

# Default target
all: $(BIN_DIR) $(BUILD_DIR) setup-mupdf build-mupdf $(TARGET)

# Setup MuPDF repository if it doesn't exist
setup-mupdf:
	@if [ ! -d "$(MUPDF_DIR)" ]; then \
		echo "Cloning MuPDF repository with submodules..."; \
		cd $(BASE_PATH) && git clone --depth 1 --branch $(MUPDF_VERSION) --recurse-submodules https://github.com/ArtifexSoftware/mupdf.git; \
		echo "Setting up MuPDF for CBR and WebP support..."; \
		echo "USE_LIBARCHIVE=yes" > $(MUPDF_DIR)/user.make; \
		echo "HAVE_WEBP=yes" >> $(MUPDF_DIR)/user.make; \
		echo "XCFLAGS += -DHAVE_LIBARCHIVE" >> $(MUPDF_DIR)/user.make; \
		echo "XCFLAGS += -DHAVE_WEBP" >> $(MUPDF_DIR)/user.make; \
		echo "SYS_LIBARCHIVE_LIBS := -larchive" >> $(MUPDF_DIR)/user.make; \
		echo "SYS_WEBP_LIBS := -lwebp -lwebpdemux" >> $(MUPDF_DIR)/user.make; \
		echo "Downloading and applying WebP patch..."; \
		if [ ! -f $(BASE_PATH)/webp-upstream-697749.patch ]; then \
			curl -o $(BASE_PATH)/webp-upstream-697749.patch \
				https://raw.githubusercontent.com/koreader/koreader-base/30ddfa05d67a8db1118326141b02bb002e2e11c4/thirdparty/mupdf/webp-upstream-697749.patch; \
		fi; \
		cd $(MUPDF_DIR) && patch -p1 < ../webp-upstream-697749.patch; \
	else \
		echo "MuPDF repository already exists"; \
	fi

# Build MuPDF with libarchive and WebP support
build-mupdf:
	@echo "Building MuPDF with libarchive and WebP support..."
	@cd $(MUPDF_DIR) && $(MAKE) HAVE_SYS_LIBARCHIVE=yes HAVE_WEBP=yes libs

# Rule to create the bin directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Rule to create the build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Link the executable
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(OBJS) $(LIBS) -o $@

# Compile source files into object files in the build directory
# This rule handles both src/ and cli/ cpp files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INC_PATHS) -c $< -o $@

$(BUILD_DIR)/%.o: $(CLI_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INC_PATHS) -c $< -o $@

# Compile Nuklear SDL renderer (C file)
$(BUILD_DIR)/nuklear_sdl_renderer.o: $(NUKLEAR_IMPL) | $(BUILD_DIR)
	$(CC) $(CXXFLAGS) $(INC_PATHS) -c $< -o $@

# Dependency installation targets for various distributions
.PHONY: install-deps install-deps-ubuntu install-deps-debian install-deps-fedora install-deps-arch
install-deps: install-deps-ubuntu

install-deps-ubuntu:
	@echo "Installing dependencies for Ubuntu/Debian..."
	sudo apt update
	sudo apt install -y build-essential pkg-config libsdl2-dev libsdl2-ttf-dev libfreetype6-dev libharfbuzz-dev libarchive-dev libwebp-dev git

install-deps-debian: install-deps-ubuntu

install-deps-fedora:
	@echo "Installing dependencies for Fedora/RHEL..."
	sudo dnf install -y gcc-c++ pkg-config SDL2-devel SDL2_ttf-devel freetype-devel harfbuzz-devel libarchive-devel libwebp-devel git

install-deps-arch:
	@echo "Installing dependencies for Arch Linux..."
	sudo pacman -S --needed base-devel pkg-config sdl2 sdl2_ttf freetype2 harfbuzz libarchive libwebp git

# Clean up compiled files and directories
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
	rm -f $(BASE_PATH)/webp-upstream-697749.patch

# Clean MuPDF (use with caution - will require re-download and rebuild)
clean-mupdf:
	@echo "Removing MuPDF directory (will be re-downloaded on next build)..."
	rm -rf $(MUPDF_DIR)

# Clean everything including downloaded dependencies
clean-all: clean clean-mupdf
	@echo "All build artifacts and dependencies cleaned"

# Help target
help:
	@echo "SDL Reader - Linux Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build the complete project (default)"
	@echo "  setup-mupdf  - Setup MuPDF library"
	@echo "  build-mupdf  - Build MuPDF with libarchive and WebP support"
	@echo "  clean        - Remove build artifacts"
	@echo "  clean-mupdf  - Remove MuPDF directory"
	@echo "  clean-all    - Remove all build artifacts and dependencies"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Dependency installation:"
	@echo "  install-deps        - Install dependencies (Ubuntu/Debian)"
	@echo "  install-deps-fedora - Install dependencies (Fedora/RHEL)"
	@echo "  install-deps-arch   - Install dependencies (Arch Linux)"
	@echo ""
	@echo "Quick start:"
	@echo "  1. make install-deps    # Install system dependencies"
	@echo "  2. make                 # Build the project"
	@echo ""
	@echo "Required system dependencies:"
	@echo "  - build-essential (gcc, g++, make)"
	@echo "  - pkg-config"
	@echo "  - libsdl2-dev"
	@echo "  - libsdl2-ttf-dev"
	@echo "  - libfreetype6-dev"
	@echo "  - libharfbuzz-dev"
	@echo "  - libarchive-dev"
	@echo "  - libwebp-dev"
	@echo "  - git"
