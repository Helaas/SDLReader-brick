# SDLReader Main Makefile

AVAILABLE_PLATFORMS := tg5040 mac
DEFAULT_PLATFORM := tg5040
PLATFORM ?= $(DEFAULT_PLATFORM)

.PHONY: all clean help list-platforms $(AVAILABLE_PLATFORMS) mac

all: $(PLATFORM)

tg5040:
	@echo "Building for TG5040..."
	$(MAKE) -f ports/tg5040/Makefile

mac:
	@echo "Building for macOS..."
	$(MAKE) -f ports/mac/Makefile

clean:
	@echo "Cleaning all platforms..."
	@$(MAKE) -f ports/tg5040/Makefile clean
	@$(MAKE) -f ports/mac/Makefile clean

list-platforms:
	@echo "Available platforms:"
	@echo "  - tg5040"
	@echo "  - mac"

help:
	@echo "SDLReader Build System"
	@echo "Usage:"
	@echo "  make         - Build for tg5040 (default)"
	@echo "  make tg5040  - Build for TG5040"
	@echo "  make mac     - Build for macOS"
	@echo "  make clean   - Clean build artifacts"
	@echo "  make help    - Show this help"
