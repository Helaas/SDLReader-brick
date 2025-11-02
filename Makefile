# SDLReader Main Makefile

AVAILABLE_PLATFORMS := tg5040 mac wiiu linux
DEFAULT_PLATFORM := tg5040
PLATFORM ?= $(DEFAULT_PLATFORM)

.PHONY: all clean clean-local help list-platforms export-tg5040 $(AVAILABLE_PLATFORMS)

all: $(PLATFORM)

tg5040:
	@echo "Building for TG5040..."
	$(MAKE) -f ports/tg5040/makefile

export-tg5040: tg5040
	@echo "Exporting TG5040 bundle..."
	$(MAKE) -f ports/tg5040/makefile export-bundle

mac:
	@echo "Building for macOS..."
	$(MAKE) -C ports/mac

wiiu:
	@echo "Building for Wii U..."
	$(MAKE) -C ports/wiiu

linux:
	@echo "Building for Linux..."
	$(MAKE) -C ports/linux

clean:
	@echo "Cleaning all platforms..."
	@$(MAKE) -C . clean-local
	-@$(MAKE) -C ports/mac clean 2>/dev/null || true
	-@$(MAKE) -C ports/wiiu clean 2>/dev/null || true
	-@$(MAKE) -C ports/linux clean 2>/dev/null || true
	-@$(MAKE) -C ports/tg5040 clean 2>/dev/null || true

clean-local:
	@echo "Cleaning build artifacts..."
	rm -rf ./build/*.o ./bin/*
	rm -rf lib/

list-platforms:
	@echo "Available platforms:"
	@echo "  - tg5040"
	@echo "  - mac"
	@echo "  - wiiu"
	@echo "  - linux"

help:
	@echo "SDLReader Build System"
	@echo "Usage:"
	@echo "  make            - Build for tg5040 (default)"
	@echo "  make tg5040     - Build for TG5040"
	@echo "  make export-tg5040 - Build and export TG5040 bundle"
	@echo "  make mac        - Build for macOS"
	@echo "  make wiiu       - Build for Wii U"
	@echo "  make linux      - Build for Linux"
	@echo "  make clean      - Clean build artifacts"
	@echo "  make help       - Show this help"
