# SDLReader Main Makefile

AVAILABLE_PLATFORMS := tg5040 mac wiiu linux
DEFAULT_PLATFORM := tg5040
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
IN_DOCKER := $(shell if [ -f /.dockerenv ]; then echo 1; elif [ -f /proc/1/cgroup ] && grep -qE '(docker|kubepods|containerd|podman)' /proc/1/cgroup; then echo 1; else echo 0; fi)

ifeq ($(origin PLATFORM), undefined)
  ifeq ($(IN_DOCKER),1)
    PLATFORM := tg5040
  else
    ifeq ($(UNAME_S),Darwin)
      PLATFORM := mac
    else
      ifeq ($(UNAME_S),Linux)
        PLATFORM := linux
      else
        PLATFORM := $(DEFAULT_PLATFORM)
      endif
    endif
  endif
endif

.PHONY: all clean clean-local help list-platforms export-tg5040 $(AVAILABLE_PLATFORMS)

all: $(PLATFORM)

tg5040:
	@echo "Building for TG5040..."
	$(MAKE) -f ports/tg5040/Makefile

export-tg5040: tg5040
	@echo "Exporting TG5040 bundle..."
	$(MAKE) -f ports/tg5040/Makefile export-bundle

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
	@echo "  make            - Build for your current platform (Docker->tg5040, macOS->mac, Linux->linux)"
	@echo "  make tg5040     - Build for TG5040"
	@echo "  make export-tg5040 - Build and export TG5040 bundle"
	@echo "  make mac        - Build for macOS"
	@echo "  make wiiu       - Build for Wii U"
	@echo "  make linux      - Build for Linux"
	@echo "  make clean      - Clean build artifacts"
	@echo "  make help       - Show this help"
