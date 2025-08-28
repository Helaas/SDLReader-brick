# Compiler and flags
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -O3 -DNDEBUG\
            -Isrc -Iinclude -D_REENTRANT -I/usr/include/SDL2

# Output locations
BIN_DIR  := bin
BUILD_DIR  := build
TARGET   := $(BIN_DIR)/sdl_reader_cli

# Object files
OBJS := $(BUILD_DIR)/pdf_document.o $(BUILD_DIR)/renderer.o $(BUILD_DIR)/text_renderer.o $(BUILD_DIR)/power_handler.o $(BUILD_DIR)/app.o $(BUILD_DIR)/main.o

# Libraries (link order matters)
LIBS := -lSDL2_ttf -lSDL2 \
  -L/usr/local/opt/mupdf-tools/lib \
  -lmupdf -lmupdf-third \
  -ljpeg -lopenjp2 -lharfbuzz -ljbig2dec \
  -lfreetype -lz -lm -lpthread -ldl

.PHONY: all clean

# Default goal
all: $(TARGET)

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

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)/*.o $(BIN_DIR)/*
