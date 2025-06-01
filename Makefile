# Makefile for SDL Reader on macOS

# Compiler
CXX = g++

# Source directory
SRC_DIR = src

# Build directory for object files
BUILD_DIR = build

# Final executable directory
BIN_DIR = bin

# Source files (all .cpp files in the src directory)
SRCS = $(wildcard $(SRC_DIR)/*.cpp)

# Executable name (including its final path)
TARGET = $(BIN_DIR)/sdl_reader

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

# pkg-config --cflags SDL2_ttf added for explicit SDL_ttf header paths
INC_PATHS = $(shell pkg-config --cflags  SDL2_ttf sdl2)

# Library paths and libraries to link
# -L$(MUPDF_OPT_PATH)/lib: Manual library path for MuPDF static libs via opt symlink
# -lmupdf -lmupdf-third: Link against MuPDF's main and third-party libraries
# pkg-config --libs SDL2_ttf added for explicit SDL_ttf library linking
LIB_PATHS = $(shell pkg-config --libs SDL2_ttf sdl2) \
            -L$(MUPDF_OPT_PATH)/lib -lmupdf -lmupdf-third 

# All object files (will be placed in the build directory)
# Transforms src/file.cpp into build/file.o
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

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
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) $(LIB_PATHS) -o $@

# Compile source files into object files in the build directory
# The -Iinclude flag is added here to ensure headers in the 'include' directory are found.
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INC_PATHS) -c $< -o $@

# Clean up compiled files and directories
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

