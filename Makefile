# SDLReader Main Makefile

AVAILABLE_PLATFORMS := tg5040 tg5050 mac wiiu linux
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

.PHONY: all clean clean-local help list-platforms export-tg5040 export-tg5050 export-trimui \
       export-tg5040-in-docker export-tg5050-in-docker export-trimui-in-docker $(AVAILABLE_PLATFORMS)

all: $(PLATFORM)

# TG5040 build targets
tg5040:
ifeq ($(IN_DOCKER),1)
	@echo "Building for TG5040..."
	$(MAKE) -f ports/tg5040/Makefile
else
	@echo "Building for TG5040 (in Docker)..."
	docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5040-toolchain:latest \
		make -C /workspace -f ports/tg5040/Makefile
endif

export-tg5040-in-docker:
	@echo "Exporting TG5040 bundle in Docker..."
	docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5040-toolchain:latest \
		/bin/sh -c "cd /workspace && make -f ports/tg5040/Makefile && make -f ports/tg5040/Makefile export-bundle"

export-tg5040:
ifeq ($(IN_DOCKER),1)
	@echo "Building TG5040..."
	$(MAKE) -f ports/tg5040/Makefile
	@echo "Exporting TG5040 bundle..."
	$(MAKE) -f ports/tg5040/Makefile export-bundle
else
	@$(MAKE) export-tg5040-in-docker
endif

# TG5050 build targets
tg5050:
ifeq ($(IN_DOCKER),1)
	@echo "Building for TG5050..."
	$(MAKE) -f ports/tg5050/Makefile
else
	@echo "Building for TG5050 (in Docker)..."
	docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5050-toolchain:latest \
		make -C /workspace -f ports/tg5050/Makefile
endif

export-tg5050-in-docker:
	@echo "Exporting TG5050 bundle in Docker..."
	docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5050-toolchain:latest \
		/bin/sh -c "cd /workspace && make -f ports/tg5050/Makefile && make -f ports/tg5050/Makefile export-bundle"

export-tg5050:
ifeq ($(IN_DOCKER),1)
	@echo "Building TG5050..."
	$(MAKE) -f ports/tg5050/Makefile
	@echo "Exporting TG5050 bundle..."
	$(MAKE) -f ports/tg5050/Makefile export-bundle
else
	@$(MAKE) export-tg5050-in-docker
endif

export-trimui-in-docker:
	@echo "Building TG5040 (in Docker)..."
	@docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5040-toolchain:latest \
		make -C /workspace -f ports/tg5040/Makefile
	@echo "Building TG5050 (in Docker)..."
	@docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5050-toolchain:latest \
		make -C /workspace -f ports/tg5050/Makefile
	@bash ports/trimui/export_bundle.sh

export-trimui:
ifeq ($(IN_DOCKER),1)
	@$(MAKE) -f ports/tg5040/Makefile
	@$(MAKE) -f ports/tg5050/Makefile
	@bash ports/trimui/export_bundle.sh
else
	@$(MAKE) export-trimui-in-docker
endif

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
	-@$(MAKE) -C ports/tg5050 clean 2>/dev/null || true

clean-local:
	@echo "Cleaning build artifacts..."
	rm -rf ./build/*.o ./bin/*
	rm -rf lib/

list-platforms:
	@echo "Available platforms:"
	@echo "  - tg5040"
	@echo "  - tg5050"
	@echo "  - mac"
	@echo "  - wiiu"
	@echo "  - linux"

help:
	@echo "SDLReader Build System"
	@echo "Usage:"
	@echo "  make            - Build for your current platform (Docker->tg5040, macOS->mac, Linux->linux)"
	@echo ""
	@echo "TrimUI (Cross-compilation - automatically uses Docker when run from host):"
	@echo "  make tg5040     - Build for TG5040 (TrimUI Brick & Smart Pro)"
	@echo "  make tg5050     - Build for TG5050 (TrimUI Smart Pro S)"
	@echo "  make export-trimui - Build and export SDLReader.pakz (TG5040 + TG5050)"
	@echo "  make export-tg5040 - Build and export TG5040-only bundle"
	@echo "  make export-tg5050 - Build and export TG5050-only bundle"
	@echo ""
	@echo "Native platforms:"
	@echo "  make mac        - Build for macOS"
	@echo "  make wiiu       - Build for Wii U"
	@echo "  make linux      - Build for Linux"
	@echo ""
	@echo "Other:"
	@echo "  make clean      - Clean build artifacts"
	@echo "  make help       - Show this help"
