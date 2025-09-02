# TG5040-specific Makefile
# This Makefile contains the build configuration specific to the TG5040 platform

# Compiler and flags
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -O3 -DNDEBUG -DTG5040_PLATFORM\
            -Isrc -Iinclude -Iports/tg5040/include -D_REENTRANT -I/usr/include/SDL2

# Output locations
BIN_DIR  := bin
BUILD_DIR  := build
TARGET   := $(BIN_DIR)/sdl_reader_cli

# Object files
OBJS := $(BUILD_DIR)/mupdf_document.o $(BUILD_DIR)/renderer.o $(BUILD_DIR)/text_renderer.o $(BUILD_DIR)/page_preloader.o $(BUILD_DIR)/tg5040_power_handler.o $(BUILD_DIR)/app.o $(BUILD_DIR)/main.o

# Libraries (link order matters)
LIBS := -lSDL2_ttf -lSDL2 \
  -L/usr/local/opt/mupdf-tools/lib \
  -lmupdf -lmupdf-third \
  -ljpeg -lopenjp2 -lharfbuzz -ljbig2dec \
  -lfreetype -lz -lm -lpthread -ldl

.PHONY: all clean export-bundle

# Default goal
all: $(TARGET)

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
