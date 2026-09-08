# SDLReader Main Makefile

AVAILABLE_PLATFORMS := tg5040 tg5050 my355 mlp1 mac wiiu linux
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
UNIVERSAL_TOOLCHAIN := ghcr.io/loveretro/tg5040-toolchain@sha256:f131c6af64029a8723d0ce8d3c2682642f5f091b04714f6beedda9bec18477ab
# Published multi-arch image; the old :local tag only ever existed on one machine.
MLP1_TOOLCHAIN ?= ghcr.io/utility-muffin-research-kitchen/mlp1-toolchain:latest

.PHONY: all native run-native run-mac run-linux universal clean clean-local help list-platforms \
       export-tg5040 export-tg5050 export-my355 export-mlp1 export-trimui export-universal export-all \
       export-tg5040-in-docker export-tg5050-in-docker export-my355-in-docker export-mlp1-in-docker export-trimui-in-docker \
       deploy deploy-platform package-mlp1 adb-stage-pak-mlp1 $(AVAILABLE_PLATFORMS)

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

universal:
	@echo "Building one NextUI binary for tg5040, tg5050, my355, and h700..."
	docker run --rm -v "$(CURDIR)":/workspace $(UNIVERSAL_TOOLCHAIN) \
		make -C /workspace -f ports/tg5040/makefile \
			PLATFORM_DEFINE=PLATFORM_NEXTUI \
			OUTPUT_DIR=/workspace/build/universal

# TG5040 build targets
tg5040:
ifeq ($(IN_DOCKER),1)
	@echo "Building for TG5040..."
	$(MAKE) -f ports/tg5040/makefile
else
	@echo "Building for TG5040 (in Docker)..."
	docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5040-toolchain:latest \
		make -C /workspace -f ports/tg5040/makefile
endif

export-tg5040-in-docker:
	@echo "Exporting TG5040 bundle in Docker..."
	docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5040-toolchain:latest \
		/bin/sh -c "cd /workspace && make -f ports/tg5040/makefile && make -f ports/tg5040/makefile export-bundle"

export-tg5040:
ifeq ($(IN_DOCKER),1)
	@echo "Building TG5040..."
	$(MAKE) -f ports/tg5040/makefile
	@echo "Exporting TG5040 bundle..."
	$(MAKE) -f ports/tg5040/makefile export-bundle
else
	@$(MAKE) export-tg5040-in-docker
endif

# TG5050 build targets
tg5050:
ifeq ($(IN_DOCKER),1)
	@echo "Building for TG5050..."
	$(MAKE) -f ports/tg5050/makefile
else
	@echo "Building for TG5050 (in Docker)..."
	docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5050-toolchain:latest \
		make -C /workspace -f ports/tg5050/makefile
endif

export-tg5050-in-docker:
	@echo "Exporting TG5050 bundle in Docker..."
	docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5050-toolchain:latest \
		/bin/sh -c "cd /workspace && make -f ports/tg5050/makefile && make -f ports/tg5050/makefile export-bundle"

export-tg5050:
ifeq ($(IN_DOCKER),1)
	@echo "Building TG5050..."
	$(MAKE) -f ports/tg5050/makefile
	@echo "Exporting TG5050 bundle..."
	$(MAKE) -f ports/tg5050/makefile export-bundle
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

# MLP1 build targets (Miniloong Pocket 1)
mlp1:
ifeq ($(IN_DOCKER),1)
	@echo "Building for MLP1..."
	$(MAKE) -f ports/mlp1/Makefile
else
	@echo "Building for MLP1 (in Docker)..."
	docker run --rm -v "$(CURDIR)":/workspace $(MLP1_TOOLCHAIN) \
		make -C /workspace -f ports/mlp1/Makefile
endif

export-mlp1-in-docker:
	@echo "Exporting MLP1 bundle in Docker..."
	docker run --rm -v "$(CURDIR)":/workspace $(MLP1_TOOLCHAIN) \
		/bin/sh -c "cd /workspace && make -f ports/mlp1/Makefile && make -f ports/mlp1/Makefile export-bundle"

export-mlp1:
ifeq ($(IN_DOCKER),1)
	@echo "Building MLP1..."
	$(MAKE) -f ports/mlp1/Makefile
	@echo "Exporting MLP1 bundle..."
	$(MAKE) -f ports/mlp1/Makefile export-bundle
else
	@$(MAKE) export-mlp1-in-docker
endif

# Package MLP1 binary into SDLReader.pak directory
package-mlp1:
	@$(MAKE) mlp1
	@echo "Creating MLP1 SDLReader.pak package..."
	@set -e; \
	PAK_DIR="build/mlp1/package/SDLReader.pak"; \
	rm -rf "$$PAK_DIR"; \
	mkdir -p "$$PAK_DIR/bin" "$$PAK_DIR/lib" "$$PAK_DIR/fonts" "$$PAK_DIR/res"; \
	cp "build/mlp1/sdl_reader_cli" "$$PAK_DIR/bin/"; \
	chmod +x "$$PAK_DIR/bin/sdl_reader_cli"; \
	cp ports/mlp1/pak/launch.sh "$$PAK_DIR/"; \
	chmod +x "$$PAK_DIR/launch.sh"; \
	bash ports/mlp1/scripts/write-pak-json.sh "$$PAK_DIR/pak.json"; \
	if [ -f res/icon.png ]; then cp res/icon.png "$$PAK_DIR/res/icon.png"; fi; \
	cp -a fonts/. "$$PAK_DIR/fonts/"; \
	echo "  Bundling libraries and stripping binary via Docker..."; \
	docker run --rm -v "$(CURDIR)":/workspace $(MLP1_TOOLCHAIN) \
		/bin/bash -c "cd /workspace && \
		TEMP=\$$(mktemp -d) && \
		BIN=./build/mlp1/sdl_reader_cli DEST=\$$TEMP PRUNE_LIBS=1 \
			bash ports/mlp1/make_bundle.sh > /dev/null 2>&1 && \
		cp -aL \$$TEMP/lib/* build/mlp1/package/SDLReader.pak/lib/ && \
		aarch64-buildroot-linux-gnu-strip build/mlp1/package/SDLReader.pak/bin/sdl_reader_cli && \
		aarch64-buildroot-linux-gnu-strip --strip-unneeded build/mlp1/package/SDLReader.pak/lib/*.so* 2>/dev/null; \
		rm -rf \$$TEMP"; \
	echo "  MLP1 PAK ready at $$PAK_DIR"

# ADB stage for MLP1
adb-stage-pak-mlp1:
	@echo "Building and packaging MLP1..."
	@$(MAKE) package-mlp1
	@echo "Staging MLP1 SDLReader.pak via ADB..."
	@bash ports/mlp1/scripts/adb-stage-pak.sh

export-trimui-in-docker:
	@echo "Building TG5040 (in Docker)..."
	@docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5040-toolchain:latest \
		make -C /workspace -f ports/tg5040/makefile
	@echo "Building TG5050 (in Docker)..."
	@docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/tg5050-toolchain:latest \
		make -C /workspace -f ports/tg5050/makefile
	@echo "Building MY355 (in Docker)..."
	@docker run --rm -v "$(CURDIR)":/workspace ghcr.io/loveretro/my355-toolchain:latest \
		make -C /workspace -f ports/my355/makefile
	@bash ports/trimui/export_bundle.sh

export-trimui:
ifeq ($(IN_DOCKER),1)
	@$(MAKE) -f ports/tg5040/makefile
	@$(MAKE) -f ports/tg5050/makefile
	@$(MAKE) -f ports/my355/makefile
	@bash ports/trimui/export_bundle.sh
else
	@$(MAKE) export-trimui-in-docker
endif

export-universal: universal
	@PLATFORMS="universal" PACKAGE_FORMAT=pak.zip \
		OUTPUT_FILE="$(CURDIR)/SDLReader.pak.zip" \
		BINARY_PLATFORM=universal BUNDLE_PLATFORM=tg5040 \
		BUNDLE_LIBS=0 \
		TOOLCHAIN_IMAGE="$(UNIVERSAL_TOOLCHAIN)" \
		bash ports/trimui/export_bundle.sh

# Default release export: one platform-neutral Pak Store archive.
export-all: export-universal

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
		*sun50iw9*|*H700*|*h700*) PLATFORM=h700 ;; \
		*rk3566*) \
			if printf '%s' "$$FINGERPRINT" | grep -qi 'miyoo-355'; then \
				PLATFORM=my355; \
			else \
				PLATFORM=mlp1; \
			fi \
			;; \
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
	@$(MAKE) universal
	@echo "Creating SDLReader.pak bundle for $(PLATFORM)..."
	@PAK_DIR="build/$(PLATFORM)/SDLReader.pak"; \
	rm -rf "$$PAK_DIR"; \
	mkdir -p "$$PAK_DIR/bin" "$$PAK_DIR/fonts" "$$PAK_DIR/res"; \
	cp "build/universal/sdl_reader_cli" "$$PAK_DIR/bin/"; \
	chmod +x "$$PAK_DIR/bin/sdl_reader_cli"; \
	if [ "$(PLATFORM)" = "mlp1" ]; then \
		cp ports/mlp1/pak/launch.sh "$$PAK_DIR/"; \
		bash ports/mlp1/scripts/write-pak-json.sh "$$PAK_DIR/pak.json"; \
		if [ -f res/icon.png ]; then cp res/icon.png "$$PAK_DIR/res/icon.png"; fi; \
	else \
		cp ports/trimui/pak-template/launch.sh "$$PAK_DIR/"; \
		cp pak.json "$$PAK_DIR/"; \
		if [ -f res/icon.png ]; then cp res/icon.png "$$PAK_DIR/res/icon.png"; fi; \
		if [ -f ports/trimui/pak-template/res/docs.pdf ]; then \
			cp ports/trimui/pak-template/res/docs.pdf "$$PAK_DIR/res/"; \
		fi; \
	fi; \
	chmod +x "$$PAK_DIR/launch.sh"; \
	cp -a fonts/. "$$PAK_DIR/fonts/"; \
	if [ "$(PLATFORM)" = "mlp1" ]; then \
		echo "  Bundling libraries and stripping binary via Docker..."; \
		docker run --rm -v "$(CURDIR)":/workspace "$(MLP1_TOOLCHAIN)" \
			/bin/bash -c "cd /workspace && \
			TEMP=\$$(mktemp -d) && \
			BIN=./build/mlp1/sdl_reader_cli DEST=\$$TEMP PRUNE_LIBS=1 \
				bash ports/mlp1/make_bundle.sh > /dev/null 2>&1 && \
			cp -aL \$$TEMP/lib/* $$PAK_DIR/lib/ && \
			aarch64-buildroot-linux-gnu-strip $$PAK_DIR/bin/sdl_reader_cli && \
			aarch64-buildroot-linux-gnu-strip --strip-unneeded $$PAK_DIR/lib/*.so* 2>/dev/null; \
			rm -rf \$$TEMP"; \
	else \
		rmdir "$$PAK_DIR/lib" 2>/dev/null || true; \
		echo "  Stripping universal binary via Docker..."; \
		docker run --rm -v "$(CURDIR)":/workspace "$(UNIVERSAL_TOOLCHAIN)" \
			strip "/workspace/$$PAK_DIR/bin/sdl_reader_cli"; \
	fi; \
	echo "  PAK ready at $$PAK_DIR"
	@ADB_CMD="$(ADB) -s $(SERIAL)"; \
	if [ "$(PLATFORM)" = "mlp1" ]; then \
		TOOLS_DIR="/mnt/sdcard/Apps/mlp1/SDLReader.pak"; \
		echo "Deploying SDLReader.pak to $$TOOLS_DIR..."; \
		$$ADB_CMD shell "rm -rf '$$TOOLS_DIR' && mkdir -p '/mnt/sdcard/Apps/mlp1'"; \
		$$ADB_CMD push "build/$(PLATFORM)/SDLReader.pak" "/mnt/sdcard/Apps/mlp1/"; \
	else \
		TOOLS_DIR="/mnt/SDCARD/Tools/$(PLATFORM)/SDLReader.pak"; \
		echo "Deploying SDLReader.pak to $$TOOLS_DIR..."; \
		$$ADB_CMD shell "rm -rf '$$TOOLS_DIR' && mkdir -p '/mnt/SDCARD/Tools/$(PLATFORM)'"; \
		$$ADB_CMD push "build/$(PLATFORM)/SDLReader.pak" "/mnt/SDCARD/Tools/$(PLATFORM)/"; \
	fi; \
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
	-@$(MAKE) -C ports/mlp1 clean 2>/dev/null || true

clean-local:
	@echo "Cleaning build artifacts..."
	rm -rf ./build/*.o ./bin/*
	rm -rf lib/
	rm -rf ./build/mlp1/

list-platforms:
	@echo "Available platforms:"
	@echo "  - tg5040"
	@echo "  - tg5050"
	@echo "  - my355"
	@echo "  - h700 (through the universal target)"
	@echo "  - mlp1"
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
	@echo "  make universal  - Build one binary for all four NextUI platforms"
	@echo "  make mlp1       - Build for MLP1 (Miniloong Pocket 1)"
	@echo "  make export-trimui - Build and export SDLReader.pakz (TG5040 + TG5050 + MY355)"
	@echo "  make export-universal - Build SDLReader.pak.zip for all four NextUI platforms"
	@echo "  make export-all - Alias for export-universal"
	@echo "  make export-tg5040 - Build and export TG5040-only bundle"
	@echo "  make export-tg5050 - Build and export TG5050-only bundle"
	@echo "  make export-my355  - Build and export MY355-only bundle"
	@echo "  make export-mlp1   - Build and export MLP1-only bundle"
	@echo "  make package-mlp1  - Build and package MLP1 .pak directory"
	@echo "  make adb-stage-pak-mlp1 - Build, package, and ADB-push for MLP1"
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
