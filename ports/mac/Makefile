# Makefile for SDL Reader on macOS (Top-Level - No Explicit Library Build)
#
# This Makefile automatically handles dependencies:
# - Checks for required system libraries (SDL2, libarchive, webp, etc.)
# - Downloads and sets up Nuklear and MuPDF if needed
# - Provides targets for dependency management
#
# Usage:
#   make install-deps  # Install system dependencies via Homebrew
#   make              # Build the project
#   make help         # Show available targets

# Compiler
CXX = g++

# Source directories
SRC_DIR = ../../src
CLI_DIR = ../../cli

# Build directory for all object files
BUILD_DIR = build

# Final executable directory (top-level bin directory)
BIN_DIR = ../../bin

# Name of the final executable
TARGET_NAME = sdl_reader_cli
TARGET = $(BIN_DIR)/$(TARGET_NAME)

# Source files: Combine all .cpp files from src/ and cli/, excluding Linux-specific files and old gui_manager
SRC_FILES = $(filter-out $(SRC_DIR)/power_handler.cpp $(SRC_DIR)/gui_manager.cpp, $(wildcard $(SRC_DIR)/*.cpp))
SRCS = $(SRC_FILES) $(wildcard $(CLI_DIR)/*.cpp)

# Nuklear is header-only, no additional source files needed
# GUI functionality will be integrated directly into gui_manager.cpp

# Local MuPDF Build Paths (using our custom build with libarchive support)
MUPDF_BUILD_PATH = mupdf/build/release

# Compiler flags
# -std=c++17: Use C++17 standard
# -Wall -Wextra: Enable common warnings
# -g: Include debugging information
# -Imupdf/include: Include path for our custom MuPDF headers with libarchive support
# -I$(SRC_DIR): Include path for project's own header files (e.g., app.h, document.h)
# -I../../thirdparty/nuklear: Include path for Nuklear headers
CXXFLAGS = -std=c++17 -Wall -Wextra -g -Imupdf/include -I$(SRC_DIR) -I../../include -I../../thirdparty/nuklear

# Include paths for SDL2, SDL2_ttf, and SDL2_image
# pkg-config --cflags SDL2_ttf SDL2_image added for explicit header paths
INC_PATHS = $(shell pkg-config --cflags SDL2_ttf SDL2_image sdl2)

# Library paths and libraries to link
# Library order matters for static libraries - dependencies should come after the libraries that use them
# MuPDF libraries must come before their dependencies (libarchive, libwebp)
# -L$(MUPDF_BUILD_PATH): Library path for our custom MuPDF static libs with libarchive and WebP support
# -lmupdf -lmupdf-third: Link against MuPDF's main and third-party libraries
# -L/opt/homebrew/opt/libarchive/lib -larchive: Link against libarchive for CBR support (must come after MuPDF)
# -L/opt/homebrew/opt/webp/lib -lwebp -lwebpdemux: Link against libwebp and libwebpdemux for WebP support (must come after MuPDF)
# pkg-config --libs SDL2_ttf SDL2_image added for library linking (can come last)
LIBS = -L$(MUPDF_BUILD_PATH) -lmupdf -lmupdf-third \
       -L/opt/homebrew/opt/libarchive/lib -larchive \
       -L/opt/homebrew/opt/webp/lib -lwebp -lwebpdemux \
       $(shell pkg-config --libs SDL2_ttf SDL2_image sdl2)

# All object files (will be placed in the build directory)
# Transforms src/file.cpp into build/file.o and cli/file.cpp into build/file.o
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%,$(SRCS))) \
       $(patsubst $(CLI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(CLI_DIR)/%,$(SRCS)))

# Nuklear is header-only, no additional object files needed
# GUI functionality will be integrated directly into gui_manager.cpp

.PHONY: all clean build-mupdf setup-mupdf clean-mupdf setup-nuklear check-deps install-deps clean-nuklear clean-all help

# Default target
all: check-deps $(BIN_DIR) $(BUILD_DIR) setup-nuklear setup-mupdf build-mupdf $(TARGET)

# Check if required system dependencies are installed
check-deps:
	@echo "Checking system dependencies..."
	@command -v pkg-config >/dev/null 2>&1 || { echo "pkg-config not found. Run 'make install-deps' to install dependencies."; exit 1; }
	@export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$$(find /opt/homebrew/Cellar -name pkgconfig -type d 2>/dev/null | tr '\n' ':')$$PKG_CONFIG_PATH"; \
	pkg-config --exists sdl2 || { echo "SDL2 not found. Run 'make install-deps' to install dependencies."; exit 1; }; \
	pkg-config --exists SDL2_ttf || { echo "SDL2_ttf not found. Run 'make install-deps' to install dependencies."; exit 1; }; \
	pkg-config --exists SDL2_image || { echo "SDL2_image not found. Run 'make install-deps' to install dependencies."; exit 1; }; \
	pkg-config --exists libarchive || { echo "libarchive not found. Run 'make install-deps' to install dependencies."; exit 1; }; \
	pkg-config --exists libwebp || { echo "libwebp not found. Run 'make install-deps' to install dependencies."; exit 1; }
	@echo "All system dependencies found!"

# Install required system dependencies using Homebrew
install-deps:
	@echo "Installing system dependencies via Homebrew..."
	@command -v brew >/dev/null 2>&1 || { echo "Homebrew not found. Please install Homebrew first: https://brew.sh"; exit 1; }
	brew install pkg-config sdl2 sdl2_ttf sdl2_image libarchive webp
	@echo "System dependencies installed successfully!"

# Setup Nuklear if it doesn't exist or is incomplete
setup-nuklear:
	@if [ ! -d "../../thirdparty/nuklear" ] || [ ! -f "../../thirdparty/nuklear/nuklear.h" ]; then \
		echo "Setting up Nuklear repository..."; \
		rm -rf ../../thirdparty/nuklear; \
		mkdir -p ../../thirdparty; \
		cd ../../thirdparty && git clone --depth 1 --branch 4.12.2 https://github.com/Immediate-Mode-UI/Nuklear.git nuklear; \
		if [ ! -f "../../thirdparty/nuklear/nuklear.h" ]; then \
			echo "Error: Nuklear setup failed - nuklear.h not found"; \
			exit 1; \
		fi; \
		echo "Nuklear setup completed successfully"; \
	else \
		echo "Nuklear repository already exists and is complete"; \
	fi

# Setup MuPDF repository if it doesn't exist
setup-mupdf:
	@if [ ! -d "mupdf" ]; then \
		echo "Cloning MuPDF repository with submodules..."; \
		git clone --depth 1 --branch 1.26.7 --recurse-submodules https://github.com/ArtifexSoftware/mupdf.git; \
		echo "Setting up MuPDF for CBR and WebP support..."; \
		echo "USE_LIBARCHIVE=yes" > mupdf/user.make; \
		echo "HAVE_WEBP=yes" >> mupdf/user.make; \
		echo "XCFLAGS += -DHAVE_LIBARCHIVE -I/opt/homebrew/opt/libarchive/include" >> mupdf/user.make; \
		echo "XCFLAGS += -DHAVE_WEBP -I/opt/homebrew/opt/webp/include" >> mupdf/user.make; \
		echo "SYS_LIBARCHIVE_CFLAGS := -I/opt/homebrew/opt/libarchive/include" >> mupdf/user.make; \
		echo "SYS_LIBARCHIVE_LIBS := -L/opt/homebrew/opt/libarchive/lib -larchive" >> mupdf/user.make; \
		echo "SYS_WEBP_CFLAGS := -I/opt/homebrew/opt/webp/include" >> mupdf/user.make; \
		echo "SYS_WEBP_LIBS := -L/opt/homebrew/opt/webp/lib -lwebp -lwebpdemux" >> mupdf/user.make; \
		echo "Downloading and applying WebP patch..."; \
		if [ ! -f webp-upstream-697749.patch ]; then \
			curl -o webp-upstream-697749.patch \
				https://raw.githubusercontent.com/koreader/koreader-base/30ddfa05d67a8db1118326141b02bb002e2e11c4/thirdparty/mupdf/webp-upstream-697749.patch; \
		fi; \
		patch -d mupdf -p1 < webp-upstream-697749.patch; \
	else \
		echo "MuPDF repository already exists"; \
	fi

# Build MuPDF with libarchive and WebP support
build-mupdf:
	@echo "Building MuPDF with libarchive and WebP support..."
	@LIBARCHIVE_PC=$$(find /opt/homebrew -name "libarchive.pc" -type f | head -1); \
	WEBP_PC=$$(find /opt/homebrew -name "libwebp.pc" -type f | head -1); \
	if [ -n "$$LIBARCHIVE_PC" ] && [ -n "$$WEBP_PC" ]; then \
		LIBARCHIVE_DIR=$$(dirname "$$LIBARCHIVE_PC"); \
		WEBP_DIR=$$(dirname "$$WEBP_PC"); \
		echo "Found libarchive.pc at: $$LIBARCHIVE_PC"; \
		echo "Found libwebp.pc at: $$WEBP_PC"; \
		echo "Using PKG_CONFIG_PATH: $$LIBARCHIVE_DIR:$$WEBP_DIR"; \
		cd mupdf && env PKG_CONFIG_PATH="$$LIBARCHIVE_DIR:$$WEBP_DIR:$$PKG_CONFIG_PATH" $(MAKE) HAVE_SYS_LIBARCHIVE=yes HAVE_WEBP=yes libs; \
	else \
		echo "Error: Required libraries not found"; \
		if [ -z "$$LIBARCHIVE_PC" ]; then echo "libarchive.pc not found"; fi; \
		if [ -z "$$WEBP_PC" ]; then echo "libwebp.pc not found"; fi; \
		exit 1; \
	fi

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

# Nuklear is header-only, no compilation rules needed

# Clean up compiled files and directories
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
	rm -f webp-upstream-697749.patch

# Clean Nuklear (use with caution - will require re-download)
clean-nuklear:
	@echo "Removing Nuklear directory (will be re-downloaded on next build)..."
	rm -rf ../../thirdparty/nuklear

# Clean MuPDF (use with caution - will require re-download and rebuild)
clean-mupdf:
	@echo "Removing MuPDF directory (will be re-downloaded on next build)..."
	rm -rf mupdf

# Clean everything including downloaded dependencies
clean-all: clean clean-nuklear clean-mupdf
	@echo "All build artifacts and dependencies cleaned"

# Show help information
help:
	@echo "SDL Reader - macOS Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build the complete project (default)"
	@echo "  check-deps   - Check if system dependencies are installed"
	@echo "  install-deps - Install system dependencies via Homebrew"
	@echo "  setup-nuklear - Setup Nuklear library"
	@echo "  setup-mupdf  - Setup MuPDF library"
	@echo "  build-mupdf  - Build MuPDF with libarchive and WebP support"
	@echo "  clean        - Remove build artifacts"
	@echo "  clean-nuklear - Remove Nuklear directory"
	@echo "  clean-mupdf  - Remove MuPDF directory"
	@echo "  clean-all    - Remove all build artifacts and dependencies"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Quick start:"
	@echo "  1. make install-deps    # Install system dependencies"
	@echo "  2. make                 # Build the project"
