# Makefile for SDL Reader on macOS (Top-Level - No Explicit Library Build)

# Compiler
CXX = g++

# Source directories
SRC_DIR = src
CLI_DIR = cli

# Build directory for all object files
BUILD_DIR = build

# Final executable directory
BIN_DIR = bin

# Name of the final executable
TARGET_NAME = sdl_reader_cli
TARGET = $(BIN_DIR)/$(TARGET_NAME)

# Source files: Combine all .cpp files from src/ and cli/, excluding Linux-specific files
SRC_FILES = $(filter-out src/power_handler.cpp, $(wildcard $(SRC_DIR)/*.cpp))
SRCS = $(SRC_FILES) $(wildcard $(CLI_DIR)/*.cpp)

# Homebrew Opt Paths (These symlinks always point to the currently active version)
# IMPORTANT: If Homebrew is installed in /opt/homebrew (Apple Silicon),
# replace /usr/local/opt with /opt/homebrew/opt
MUPDF_OPT_PATH = /usr/local/opt/mupdf-tools

# Compiler flags
# -std=c++17: Use C++17 standard
# -Wall -Wextra: Enable common warnings
# -g: Include debugging information
# -I$(MUPDF_OPT_PATH)/include: Manual include path for MuPDF headers via opt symlink
# -I$(SRC_DIR): Include path for project's own header files (e.g., app.h, document.h)
CXXFLAGS = -std=c++17 -Wall -Wextra -g -I$(MUPDF_OPT_PATH)/include -I$(SRC_DIR) -Iinclude

# Include paths for SDL2 and SDL2_ttf
# pkg-config --cflags SDL2_ttf added for explicit SDL_ttf header paths
INC_PATHS = $(shell pkg-config --cflags SDL2_ttf sdl2)

# Library paths and libraries to link
# -L$(MUPDF_OPT_PATH)/lib: Manual library path for MuPDF static libs via opt symlink
# -lmupdf -lmupdf-third: Link against MuPDF's main and third-party libraries
# pkg-config --libs SDL2_ttf added for explicit SDL_ttf library linking
LIBS = $(shell pkg-config --libs SDL2_ttf sdl2) \
       -L$(MUPDF_OPT_PATH)/lib -lmupdf -lmupdf-third

# All object files (will be placed in the build directory)
# Transforms src/file.cpp into build/file.o and cli/file.cpp into build/file.o
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(SRC_DIR)/%,$(SRCS))) \
       $(patsubst $(CLI_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter $(CLI_DIR)/%,$(SRCS)))

.PHONY: all clean

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
	$(CXX) $(OBJS) $(LIBS) -o $@

# Compile source files into object files in the build directory
# This rule handles both src/ and cli/ cpp files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INC_PATHS) -c $< -o $@

$(BUILD_DIR)/%.o: $(CLI_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INC_PATHS) -c $< -o $@

# Clean up compiled files and directories
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
