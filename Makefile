# Compiler and flags
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g \
            -Isrc -Iinclude -D_REENTRANT -I/usr/include/SDL2

# Output locations
BIN_DIR  := $(HOME)/SDLReader/bin
TARGET   := $(BIN_DIR)/sdl_reader_cli

# Object files
OBJS := build/pdf_document.o build/renderer.o build/text_renderer.o build/app.o build/main.o

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

# Compile source to object files
build/%.o: src/%.cpp
	 @mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile sources from cli/
build/%.o: cli/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf build/*.o $(BIN_DIR)/*
