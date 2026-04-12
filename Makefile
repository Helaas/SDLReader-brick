# SDLReader Main Makefile

AVAILABLE_PLATFORMS := tg5040 tg5050 my355 mac wiiu linux
DEFAULT_PLATFORM := tg5040
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
IN_DOCKER := $(shell if [ -f /.dockerenv ]; then echo 1; elif [ -f /proc/1/cgroup ] && grep -qE '(docker|kubepods|containerd|podman)' /proc/1/cgroup; then echo 1; else echo 0; fi)
RUN_ARGS ?= --browse

ifeq ($(UNAME_S),Darwin)
  NATIVE_PLATFORM := mac
else ifeq ($(UNAME_S),Linux)
  NATIVE_PLATFORM := linux
else
  NATIVE_PLATFORM := unsupported
endif

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

ADB ?= adb

.PHONY: all native run-native run-mac run-linux clean clean-local help list-platforms export-tg5040 export-tg5050 export-my355 export-trimui export-all \
       export-tg5040-in-docker export-tg5050-in-docker export-my355-in-docker export-trimui-in-docker \
       deploy deploy-platform $(AVAILABLE_PLATFORMS)

all: $(PLATFORM)

native:
ifeq ($(NATIVE_PLATFORM),unsupported)
	@echo "Error: native builds are only supported on macOS and Linux hosts."
	@exit 1
else
	@$(MAKE) $(NATIVE_PLATFORM)
endif

run-native:
ifeq ($(NATIVE_PLATFORM),unsupported)
	@echo "Error: native runs are only supported on macOS and Linux hosts."
	@exit 1
else
	@$(MAKE) run-$(NATIVE_PLATFORM) RUN_ARGS='$(RUN_ARGS)'
endif

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

# MY355 build targets (Miyoo Flip)
my355:
ifeq ($(IN_DOCKER),1)
	@echo "Building for MY355..."
	$(MAKE) -f ports/my355/makefile
else
	@echo "Building for MY355 (in Docker)..."
	docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/my355-toolchain:latest \
		make -C /workspace -f ports/my355/makefile
endif

export-my355-in-docker:
	@echo "Exporting MY355 bundle in Docker..."
	docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/my355-toolchain:latest \
		/bin/sh -c "cd /workspace && make -f ports/my355/makefile && make -f ports/my355/makefile export-bundle"

export-my355:
ifeq ($(IN_DOCKER),1)
	@echo "Building MY355..."
	$(MAKE) -f ports/my355/makefile
	@echo "Exporting MY355 bundle..."
	$(MAKE) -f ports/my355/makefile export-bundle
else
	@$(MAKE) export-my355-in-docker
endif

export-trimui-in-docker:
	@echo "Building TG5040 (in Docker)..."
	@docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5040-toolchain:latest \
		make -C /workspace -f ports/tg5040/Makefile
	@echo "Building TG5050 (in Docker)..."
	@docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5050-toolchain:latest \
		make -C /workspace -f ports/tg5050/Makefile
	@echo "Building MY355 (in Docker)..."
	@docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/my355-toolchain:latest \
		make -C /workspace -f ports/my355/makefile
	@bash ports/trimui/export_bundle.sh

export-trimui:
ifeq ($(IN_DOCKER),1)
	@$(MAKE) -f ports/tg5040/Makefile
	@$(MAKE) -f ports/tg5050/Makefile
	@$(MAKE) -f ports/my355/makefile
	@bash ports/trimui/export_bundle.sh
else
	@$(MAKE) export-trimui-in-docker
endif

# Alias for exporting all NextUI device bundles into SDLReader.pakz.
export-all:
	@$(MAKE) export-trimui

# ADB deploy - auto-detect platform and push SDLReader.pak to device
deploy:
	@echo "Detecting platform..."
	@SERIAL="$(ADB_SERIAL)"; \
	if [ -z "$$SERIAL" ]; then \
		SERIAL=$$($(ADB) devices | awk 'NR>1 && $$2=="device" {print $$1; exit}'); \
	fi; \
	if [ -z "$$SERIAL" ]; then \
		echo "Error: No online adb device found."; \
		exit 1; \
	fi; \
	ADB_CMD="$(ADB) -s $$SERIAL"; \
	FINGERPRINT=$$($$ADB_CMD shell ' \
		cat /proc/device-tree/compatible 2>/dev/null; \
		echo; \
		cat /proc/device-tree/model 2>/dev/null; \
		echo; \
		uname -a 2>/dev/null' 2>/dev/null | tr '\000' '\n' | tr -d '\r'); \
	case "$$FINGERPRINT" in \
		*rk3566*|*miyoo-355*) PLATFORM=my355 ;; \
		*allwinner,a523*|*sun55iw3*) PLATFORM=tg5050 ;; \
		*allwinner,a133*|*sun50iw*) PLATFORM=tg5040 ;; \
		*allwinner*) \
			if printf '%s' "$$FINGERPRINT" | grep -qi 'a523'; then \
				PLATFORM=tg5050; \
			else \
				PLATFORM=tg5040; \
			fi \
			;; \
		*) \
			echo "Error: Could not detect a supported platform from adb fingerprint."; \
			echo "  Serial: $$SERIAL"; \
			echo "  Fingerprint: $$(printf '%s' "$$FINGERPRINT" | head -c 240)"; \
			exit 1; \
			;; \
	esac; \
	echo "Detected adb serial: $$SERIAL"; \
	echo "Detected platform: $$PLATFORM"; \
	$(MAKE) deploy-platform PLATFORM=$$PLATFORM SERIAL=$$SERIAL

deploy-platform:
	@if [ -z "$(PLATFORM)" ] || [ -z "$(SERIAL)" ]; then \
		echo "Error: deploy-platform requires PLATFORM and SERIAL."; \
		exit 1; \
	fi
	@$(MAKE) $(PLATFORM)
	@echo "Creating SDLReader.pak bundle for $(PLATFORM)..."
	@PAK_DIR="build/$(PLATFORM)/SDLReader.pak"; \
	rm -rf "$$PAK_DIR"; \
	mkdir -p "$$PAK_DIR/bin" "$$PAK_DIR/lib" "$$PAK_DIR/fonts" "$$PAK_DIR/res"; \
	cp "build/$(PLATFORM)/sdl_reader_cli" "$$PAK_DIR/bin/"; \
	chmod +x "$$PAK_DIR/bin/sdl_reader_cli"; \
	cp ports/trimui/pak-template/launch.sh "$$PAK_DIR/"; \
	chmod +x "$$PAK_DIR/launch.sh"; \
	cp pak.json "$$PAK_DIR/"; \
	cp -a fonts/. "$$PAK_DIR/fonts/"; \
	if [ -f ports/trimui/pak-template/res/docs.pdf ]; then \
		cp ports/trimui/pak-template/res/docs.pdf "$$PAK_DIR/res/"; \
	fi; \
	echo "  Bundling libraries and stripping binary via Docker..."; \
	TOOLCHAIN="ghcr.io/loveretro/$(PLATFORM)-toolchain:latest"; \
	MAKEFILE=$$([ "$(PLATFORM)" = "my355" ] && echo "ports/my355/makefile" || echo "ports/$(PLATFORM)/Makefile"); \
	docker run --rm -v "$(CURDIR)":/workspace "$$TOOLCHAIN" \
		/bin/bash -c "cd /workspace && \
		TEMP=\$$(mktemp -d) && \
		BIN=./build/$(PLATFORM)/sdl_reader_cli DEST=\$$TEMP PRUNE_LIBS=1 \
			bash ports/$(PLATFORM)/make_bundle.sh > /dev/null 2>&1 && \
		cp -aL \$$TEMP/lib/* $$PAK_DIR/lib/ && \
		strip $$PAK_DIR/bin/sdl_reader_cli && \
		strip --strip-unneeded $$PAK_DIR/lib/*.so* 2>/dev/null; \
		rm -rf \$$TEMP"; \
	echo "  PAK ready at $$PAK_DIR"
	@ADB_CMD="$(ADB) -s $(SERIAL)"; \
	TOOLS_DIR="/mnt/SDCARD/Tools/$(PLATFORM)/SDLReader.pak"; \
	echo "Deploying SDLReader.pak to $$TOOLS_DIR..."; \
	$$ADB_CMD shell "rm -rf '$$TOOLS_DIR' && mkdir -p '/mnt/SDCARD/Tools/$(PLATFORM)'"; \
	$$ADB_CMD push "build/$(PLATFORM)/SDLReader.pak" "/mnt/SDCARD/Tools/$(PLATFORM)/"; \
	echo "Deploy complete."

mac:
	@echo "Building for macOS..."
	$(MAKE) -C ports/mac

run-mac: mac
	./bin/sdl_reader_cli $(RUN_ARGS)

wiiu:
	@echo "Building for Wii U..."
	$(MAKE) -C ports/wiiu

linux:
	@echo "Building for Linux..."
	$(MAKE) -C ports/linux

run-linux: linux
	./bin/sdl_reader_cli $(RUN_ARGS)

clean:
	@echo "Cleaning all platforms..."
	@$(MAKE) -C . clean-local
	-@$(MAKE) -C ports/mac clean 2>/dev/null || true
	-@$(MAKE) -C ports/wiiu clean 2>/dev/null || true
	-@$(MAKE) -C ports/linux clean 2>/dev/null || true
	-@$(MAKE) -C ports/tg5040 clean 2>/dev/null || true
	-@$(MAKE) -C ports/tg5050 clean 2>/dev/null || true
	-@$(MAKE) -C ports/my355 clean 2>/dev/null || true

clean-local:
	@echo "Cleaning build artifacts..."
	rm -rf ./build/*.o ./bin/*
	rm -rf lib/

list-platforms:
	@echo "Available platforms:"
	@echo "  - tg5040"
	@echo "  - tg5050"
	@echo "  - my355"
	@echo "  - mac"
	@echo "  - wiiu"
	@echo "  - linux"

help:
	@echo "SDLReader Build System"
	@echo "Usage:"
	@echo "  make            - Build for your current platform (Docker->tg5040, macOS->mac, Linux->linux)"
	@echo ""
	@echo "TrimUI / NextUI (Cross-compilation - automatically uses Docker when run from host):"
	@echo "  make tg5040     - Build for TG5040 (TrimUI Brick & Smart Pro)"
	@echo "  make tg5050     - Build for TG5050 (TrimUI Smart Pro S)"
	@echo "  make my355      - Build for MY355 (Miyoo Flip)"
	@echo "  make export-trimui - Build and export SDLReader.pakz (TG5040 + TG5050 + MY355)"
	@echo "  make export-all - Alias for export-trimui (TG5040 + TG5050 + MY355)"
	@echo "  make export-tg5040 - Build and export TG5040-only bundle"
	@echo "  make export-tg5050 - Build and export TG5050-only bundle"
	@echo "  make export-my355  - Build and export MY355-only bundle"
	@echo ""
	@echo "Native platforms:"
	@echo "  make native     - Build for the current host OS (macOS/Linux)"
	@echo "  make run-native - Build and run on the current host OS (default: RUN_ARGS='--browse')"
	@echo "  make mac        - Build for macOS"
	@echo "  make run-mac    - Build and run for macOS"
	@echo "  make wiiu       - Build for Wii U"
	@echo "  make linux      - Build for Linux"
	@echo "  make run-linux  - Build and run for Linux"
	@echo "                   Override args with RUN_ARGS='path/to/file.pdf' or RUN_ARGS='--browse --show-filebrowser-images'"
	@echo ""
	@echo "Deploy:"
	@echo "  make deploy     - Detect adb device, build, and push SDLReader.pak"
	@echo ""
	@echo "Other:"
	@echo "  make clean      - Clean build artifacts"
	@echo "  make help       - Show this help"
