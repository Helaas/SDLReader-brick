# Makefile for SDL Reader on macOS (Top-Level - No Explicit Library Build)

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

# Source files: Combine all .cpp files from src/ and cli/, excluding Linux-specific files
SRC_FILES = $(filter-out src/power_handler.cpp, $(wildcard $(SRC_DIR)/*.cpp))
SRCS = $(SRC_FILES) $(wildcard $(CLI_DIR)/*.cpp)

# Local MuPDF Build Paths (using our custom build with libarchive support)
MUPDF_BUILD_PATH = ../mupdf-build/1.26.7/build/release

# Compiler flags
# -std=c++17: Use C++17 standard
# -Wall -Wextra: Enable common warnings
# -g: Include debugging information
# -I../mupdf-build/1.26.7/include: Include path for our custom MuPDF headers with libarchive support
# -I$(SRC_DIR): Include path for project's own header files (e.g., app.h, document.h)
CXXFLAGS = -std=c++17 -Wall -Wextra -g -I../mupdf-build/1.26.7/include -I$(SRC_DIR) -I../../include

# Include paths for SDL2, SDL2_ttf, and SDL2_image
# pkg-config --cflags SDL2_ttf SDL2_image added for explicit header paths
INC_PATHS = $(shell pkg-config --cflags SDL2_ttf SDL2_image sdl2)

# Library paths and libraries to link
# Library order matters for static libraries - dependencies should come after the libraries that use them
# MuPDF libraries must come before their dependencies (libarchive)
# -L$(MUPDF_BUILD_PATH): Library path for our custom MuPDF static libs with libarchive support
# -lmupdf -lmupdf-third: Link against MuPDF's main and third-party libraries
# -L/opt/homebrew/opt/libarchive/lib -larchive: Link against libarchive for CBR support (must come after MuPDF)
# pkg-config --libs SDL2_ttf SDL2_image added for library linking (can come last)
LIBS = -L$(MUPDF_BUILD_PATH) -lmupdf -lmupdf-third \
       -L/opt/homebrew/opt/libarchive/lib -larchive \
       $(shell pkg-config --libs SDL2_ttf SDL2_image sdl2)

# All object files (will be placed in the build directory)
# Transforms src/file.cpp into build/file.o and cli/file.cpp into build/file.o
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%,$(SRCS))) \
       $(patsubst $(CLI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(CLI_DIR)/%,$(SRCS)))

.PHONY: all clean build-mupdf

# Default target
all: $(BIN_DIR) $(BUILD_DIR) build-mupdf $(TARGET)

# Build MuPDF with libarchive support
build-mupdf:
	@echo "Building MuPDF with libarchive support..."
	@cd ../mupdf-build/1.26.7 && $(MAKE) libs

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

# Clean up compiled files and directories
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
