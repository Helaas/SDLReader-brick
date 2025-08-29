# Makefile for SDL Reader on Linux

# Compiler
CXX = g++

# Source directories
SRC_DIR = ../../src
CLI_DIR = ../../cli
INCLUDE_DIR = ../../include

# Build directory for all object files
BUILD_DIR = build

# Final executable directory (project root)
BIN_DIR = ../../bin

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
CXXFLAGS = -std=c++17 -Wall -Wextra -g -I$(INCLUDE_DIR)

# Include paths for SDL2 and SDL2_ttf
INC_PATHS = $(shell pkg-config --cflags sdl2 SDL2_ttf freetype2 harfbuzz)

# Library paths and libraries to link
# Using pkg-config for available libraries and manual linking for MuPDF dependencies
LIBS = $(shell pkg-config --libs sdl2 SDL2_ttf freetype2 harfbuzz) -lmupdf -lz -lm

# All object files (will be placed in the build directory)
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%,$(SRCS))) \
       $(patsubst $(CLI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(CLI_DIR)/%,$(SRCS)))

.PHONY: all clean install-deps help

# Default target
all: $(BIN_DIR) $(BUILD_DIR) $(TARGET)

# Rule to create the bin directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Rule to create the build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Link the executable
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(OBJS) $(shell pkg-config --libs sdl2 SDL2_ttf) -lmupdf -lmupdf-third -lfreetype -lharfbuzz -ljpeg -lopenjp2 -ljbig2dec -lgumbo -lmujs -lz -lm -lpthread -o $@

# Compile source files into object files in the build directory
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INC_PATHS) -c $< -o $@

$(BUILD_DIR)/%.o: $(CLI_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INC_PATHS) -c $< -o $@

# Install dependencies (Ubuntu/Debian)
install-deps:
	@echo "Installing dependencies for Ubuntu/Debian..."
	sudo apt update
	sudo apt install -y build-essential pkg-config libsdl2-dev libsdl2-ttf-dev libmupdf-dev libfreetype6-dev libharfbuzz-dev libjpeg-dev libopenjp2-7-dev libjbig2dec0-dev libgumbo-dev libmujs-dev

# Install dependencies for other distributions
install-deps-fedora:
	@echo "Installing dependencies for Fedora/RHEL..."
	sudo dnf install -y gcc-c++ pkg-config SDL2-devel SDL2_ttf-devel mupdf-devel freetype-devel

install-deps-arch:
	@echo "Installing dependencies for Arch Linux..."
	sudo pacman -S --needed base-devel pkg-config sdl2 sdl2_ttf mupdf freetype2

# Clean up compiled files and directories
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)

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
	@echo "  - libmupdf-dev"
	@echo "  - libfreetype6-dev"
	@echo "  - libharfbuzz-dev"
	@echo "  - libjpeg-dev"
	@echo "  - libopenjp2-7-dev"
	@echo "  - libjbig2dec0-dev"
	@echo "  - libgumbo-dev"
	@echo "  - libmujs-dev"
