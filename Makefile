# SDLReader Main Makefile

AVAILABLE_PLATFORMS := tg5040 mac wiiu
DEFAULT_PLATFORM := tg5040
PLATFORM ?= $(DEFAULT_PLATFORM)

.PHONY: all clean help list-platforms $(AVAILABLE_PLATFORMS)

all: $(PLATFORM)

tg5040:
	@echo "Building for TG5040..."
	$(MAKE) -f ports/tg5040/Makefile

mac:
	@echo "Building for macOS..."
	$(MAKE) -f ports/mac/Makefile

wiiu:
	@echo "Building for Wii U..."
	$(MAKE) -C ports/wiiu

clean:
	@echo "Cleaning all platforms..."
	@$(MAKE) -f ports/tg5040/Makefile clean
	@$(MAKE) -f ports/mac/Makefile clean
	@$(MAKE) -C ports/wiiu clean

list-platforms:
	@echo "Available platforms:"
	@echo "  - tg5040"
	@echo "  - mac"
	@echo "  - wiiu"

help:
	@echo "SDLReader Build System"
	@echo "Usage:"
	@echo "  make         - Build for tg5040 (default)"
	@echo "  make tg5040  - Build for TG5040"
	@echo "  make mac     - Build for macOS"
	@echo "  make wiiu    - Build for Wii U"
	@echo "  make clean   - Clean build artifacts"
	@echo "  make help    - Show this help"
