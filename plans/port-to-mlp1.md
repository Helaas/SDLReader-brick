# Port SDLReader to Miniloong Pocket 1 (MLP1)

## Overview

Port the SDLReader document viewer to the Miniloong Pocket 1 (MLP1) handheld,
targeting the **Leaf** launcher ecosystem (not NextUI). The MLP1 uses an
RK3566 (Cortex-A55), **960x720 landscape** (720x960 portrait panel; Weston
compositor applies `transform=rotate-90`), Loong Gamepad input, and Wayland
SDL video. This port creates a separate build artifact (`.pak` directory)
deployed
to `Apps/mlp1/` on the device SD card, independent of the NextUI Pak Store
bundles.

---

## Implementation Status & Corrections (2026-06-07)

**Status: builds, packages, deploys, and runs on a real device.** Verified end to
end: `make mlp1` → aarch64 binary; `make package-mlp1` → stripped `.pak`;
`make adb-stage-pak-mlp1` → adb push to `/mnt/sdcard/Apps/mlp1/SDLReader.pak/`;
on-device launch brings up the Wayland window (Mali g13p0), loads fonts, scans the
SD card, and saves config.

### Deployment: direct ADB, NOT symlinks (supersedes Phase 3.4 / 3.5)
The Leaf-workspace **symlink** approach (Phase 3.4/3.5) is **not used**. Deployment
is a self-contained make target: `make adb-stage-pak-mlp1` (build + package + adb
push + chmod), implemented by `ports/mlp1/scripts/adb-stage-pak.sh`. `make deploy`
also auto-detects `*rk3566*` (non-`miyoo-355`) → mlp1 and pushes to the same path.
Phases 3.4/3.5 below are obsolete and kept only for history.

### Build blockers the plan did not anticipate (all fixed)
The `mlp1-toolchain:local` image and the copied-from-my355 Makefile assumptions did
not hold:
1. Image lacked `curl`/`wget` → libarchive/webp/mupdf-patch downloads failed.
   Added to `mlp1-toolchain/Dockerfile`.
2. Image lacked `patch` → MuPDF WebP patch failed. Added to the Dockerfile.
3. `PKG_CONFIG` was `aarch64-buildroot-linux-gnu-pkg-config` (does not exist).
   The SDK's sysroot-aware wrapper is plain `pkg-config`. Fixed.
4. SDK sysroot ships **only zlib** (no bz2/lzma/lz4/zstd). **Full parity has since
   been implemented**: bzip2 1.0.8, xz 5.4.6, lz4 1.9.4 and zstd 1.5.6 are built as
   static `-fPIC` libs from source (`build-bzip2/xz/lz4/zstd` → workspace-relative
   prefixes), libarchive is configured `--with-bz2lib --with-lzma --with-lz4
   --with-zstd` (all detected `yes`), and the binary links `-lbz2 -llzma -llz4
   -lzstd`. Verified: codec symbols present in the binary; runs on device.
5. libarchive installed to `/usr/local/libarchive-minimal` → the Buildroot gcc
   wrapper is paranoid and fatally rejects `/usr/local/lib*` paths. Moved to a
   workspace-relative `LIBARCHIVE_PREFIX` (like `WEBP_PREFIX`).
6. `power_handler.cpp` missing `<cstdio>` (snprintf) and `<string>` (const char* →
   std::string callback) — fixed includes.
7. `export_bundle.sh` did `rm -rf ports/mlp1/pak`, deleting the committed
   `launch.sh`/`pak.json` — fixed to clean only generated subdirs.
8. `package-mlp1`/`deploy-platform` used plain `strip` (absent in the mlp1 image) →
   binary shipped unstripped. Now uses the cross strip.

### Remaining decisions / unverified risks
- **Archive compression**: ✅ done — full bz2/lzma/lz4/zstd parity (see #4).
- **SDL2 bundling**: device SDL2/SDL2_ttf/wayland versions exactly match the
  sysroot, so SDL2 is left to the device (not bundled); `make_bundle.sh` bundles
  libstdc++/libz. Fine as-is.
- **Power button**: ✅ verified working on-device — loong_power does not block the
  read-only observer.
- **Loong Gamepad** enumerates as two input nodes — verify SDL controller mapping.

---

## Phase 1: Build System — New Platform Port (`ports/mlp1/`)

### 1.1 Create `ports/mlp1/Makefile`

Modeled on `ports/my355/makefile` but adapted for the MLP1 toolchain.

| Setting | MLP1 Value |
|---------|------------|
| Toolchain image | `ghcr.io/utility-muffin-research-kitchen/mlp1-toolchain:local` |
| Cross prefix | `aarch64-buildroot-linux-gnu-` |
| Sysroot | `/opt/mlp1-toolchain/aarch64-buildroot-linux-gnu/sysroot/usr` |
| CPU tuning | `-mcpu=cortex-a55 -mtune=cortex-a55` |
| Platform define | `-DPLATFORM_MLP1` (do **not** set `-DTRIMUI_PLATFORM`) |
| Nuklear clone dir | `nuklear-mlp1` (subdir under `ports/mlp1/`) |
| Build dir | `build/mlp1/` |

**Key structural differences from MY355:**
- SDL2/SDL2_ttf come from the MLP1 Buildroot SDK sysroot via pkg-config, not
  NextUI's sysroot prefix. Include flags should use pkg-config (like the smoke
  test: `$(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf)`).
- Power handler — see Phase 2.6. MLP1 needs one because there is no NextUI
  power manager. The stock `loong_power` daemon owns the power button, so
  SDLReader's handler watches it read-only (no grab) for long-press detection
  and graceful shutdown, while short-press suspend is delegated to loong_power.
- MuPDF, libarchive, and WebP build identical to other platforms (they are
  static libraries built in-container).

**Files to create:**
- `ports/mlp1/Makefile` — full platform Makefile
- `ports/mlp1/export_bundle.sh` — placeholder or MLP1-specific bundle script
- `ports/mlp1/make_bundle.sh` — library bundling script (minimal; most deps
  static-linked into the binary)

### 1.2 Nuklear Checkout

The `ports/mlp1/Makefile`'s `setup-nuklear` target clones Nuklear 4.12.8 into
`ports/mlp1/nuklear-mlp1/` (same pattern as `nuklear-tg5040`, `nuklear-my355`).
No code changes needed — Nuklear is header-only and works identically on SDL2.

### 1.3 Update Root `Makefile`

Add MLP1 targets to `/Volumes/Storage/GitHub/SDLReader-brick/Makefile`:

- `AVAILABLE_PLATFORMS` += `mlp1`
- `mlp1:` target — runs Docker with `ghcr.io/utility-muffin-research-kitchen/mlp1-toolchain:local`
  and invokes `ports/mlp1/Makefile`
- `export-mlp1:` target — build + export MLP1 bundle
- `package-mlp1:` target — build MLP1 binary then assemble `.pak` directory
- `deploy` target — add MLP1 fingerprint detection (`*rk3566*`) to the ADB
  fingerprint case statement
- `deploy-platform` target — handle MLP1 `.pak` deployment to
  `/mnt/sdcard/Apps/mlp1/SDLReader.pak` (distinct from NextUI's
  `/mnt/SDCARD/Tools/...` path)
- `clean` target — add `ports/mlp1` cleanup
- `list-platforms`, `help` — add MLP1 entries

**Critical**: The `deploy-platform` target currently hardcodes NextUI paths
(`/mnt/SDCARD/Tools/$(PLATFORM)/SDLReader.pak`). For MLP1, the target must be
`/mnt/sdcard/Apps/mlp1/SDLReader.pak`. The existing `deploy` flow for
TG5040/TG5050/MY355 can remain unchanged; MLP1 needs its own deployment logic.

---

## Phase 2: Code Changes — Platform Abstraction

### 2.1 Window Resolution (`cli/main.cpp`)

Add `PLATFORM_MLP1` case block alongside existing `PLATFORM_MY355` / `TRIMUI_PLATFORM`:

| Platform | Width | Height |
|----------|-------|--------|
| MLP1 | 960 | 720 |
| MY355 | 640 | 480 |
| TG5040/TG5050 | 1280 | 720 |
| Desktop | 1280 | 960 |

The physical panel is 720x960 portrait; Weston's `transform=rotate-90` in
`weston.ini` makes the logical display 960x720 landscape for Wayland clients.
SDL on Wayland gets the compositor-rotated size, so create a 960x720 window.

The `SDL_CreateWindow` call uses `SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE` —
verify this works with the Wayland SDL backend (`SDL_VIDEODRIVER=wayland`).
Leaf's launch script will export this before launching SDLReader. No fullscreen
flag needed; Leaf manages fullscreen.

**File:** `cli/main.cpp` lines 32–42

### 2.2 Button Mapper (`src/button_mapper.cpp`, `include/button_mapper.h`)

The MLP1 uses the **Loong Gamepad** which registers as a USB joystick/gamepad.
SDL detects it via the SDL GameController API, but SDL's A/B controller labels
are swapped versus the physical shell buttons. Map by physical button, matching
Jawaka/Catastrophe behavior:

- A (bottom) → Accept
- B (right) → Cancel
- X (left) → Alternate
- Y (top) → Special
- L1 → PagePrevious
- R1 → PageNext
- Start → Menu
- Select → Options (MirrorHorizontal?)
- D-pad → navigation
- Left stick → analog scroll (already handled by SDL axis events)

Keep this scoped to the `#elif defined(PLATFORM_MLP1)` branch so the
`TRIMUI_PLATFORM` / NextUI mappings remain unchanged.

**Key:**
- `m_buttonMap[SDL_CONTROLLER_BUTTON_A] = LogicalButton::Cancel;`
- `m_buttonMap[SDL_CONTROLLER_BUTTON_B] = LogicalButton::Accept;`
- `m_buttonMap[SDL_CONTROLLER_BUTTON_START] = LogicalButton::Menu;` (open
  settings, not MirrorHorizontal like TG5040)
- `m_buttonMap[SDL_CONTROLLER_BUTTON_BACK] = LogicalButton::Options;`
- `m_buttonMap[SDL_CONTROLLER_BUTTON_GUIDE] = LogicalButton::Quit;`

The standalone file browser has its own SDL event loop. It must mirror this
physical A/B behavior for `PLATFORM_MLP1` without changing the existing
`TRIMUI_PLATFORM` branch.

The settings UI (`GuiManager`) also has local controller constants for dropdowns,
menus, and the number pad. Its `PLATFORM_MLP1` branch must use the same physical
A/B mapping.

Also update `getPlatformName()` to return `"MLP1"` for the `PLATFORM_MLP1`
case.

**Files:** `src/button_mapper.cpp`, `include/button_mapper.h`

### 2.3 Platform Constants (`include/platform_constants.h`)

Add `PLATFORM_MLP1` to the conditional branches. The Loong Gamepad has decent
joysticks — start with MY355's conservative values (dead zone 20000, repeat
delays 220/130) and adjust after real-device testing.

```cpp
#ifdef PLATFORM_MY355
    // ...existing MY355 values
#elif defined(PLATFORM_MLP1)
    inline constexpr Sint16 AXIS_DEAD_ZONE = 20000;
    inline constexpr Uint32 INPUT_INITIAL_DELAY_MS = 220;
    inline constexpr Uint32 INPUT_REPEAT_DELAY_MS = 130;
#else
    // ...existing defaults
#endif
```

**File:** `include/platform_constants.h`

### 2.4 Thumbnail Dimensions (`include/file_browser.h`)

Add `PLATFORM_MLP1` to the `THUMBNAIL_MAX_DIM` conditional in the header
(around the `#ifdef TRIMUI_PLATFORM` block). MLP1's 960x720 screen is relatively large (same pixel count as 800x600),
so start with 120 (same as MY355) and tune upward if performance permits.

**Note on approach:** The thumbnail dims are currently set in
`file_browser.h`. If the existing code uses `TRIMUI_PLATFORM` check + no-if
fallback, wrap the MLP1 case into that chain. The cleanest approach: use a
fine-grained `ifdef` ladder with all platforms explicit.

**File:** `include/file_browser.h`

### 2.5 Viewport Timing (`include/viewport_manager.h`)

The `ZOOM_DEBOUNCE_MS` is set to 250 when `TRIMUI_PLATFORM` and 75 otherwise.
MLP1 should use 250 (device-grade input debounce, same as other handhelds).
No code change needed if `TRIMUI_PLATFORM` is not set for MLP1 — but we may
want an explicit `PLATFORM_MLP1` branch. TBD based on real-device testing.

**File:** `include/viewport_manager.h` (verify at implementation time)

### 2.6 Power Management (`ports/mlp1/include/power_handler.h` + `ports/mlp1/src/power_handler.cpp`)

The MLP1 power architecture is fundamentally different from NextUI devices
(TG5040/TG5050/MY355). On NextUI, SDLReader's `PowerHandler` owns the power
button entirely. On MLP1, the **stock `loong_power` daemon** runs as PID 1 and
owns the physical power button event device (it has the exclusive grab).
`loong_power` handles:
- Short press → system suspend (to `mem`, single-button wake)
- Long press (hold 3s+) → poweroff
- Auto-sleep via `loong_power.cfg` (`screenLockTimeout`, `hibernateDisable`)
- Backlight brightness via `bl_power`

SDLReader's power handler must therefore take a **read-only observer** approach,
modeled on `Jawaka/internal/platform/input_proxy_mlp1.c`:

#### Power Handler Design

| Feature | Approach | Source |
|---------|----------|--------|
| Power input device | Dynamic discovery via `EVIOCGBIT(EV_KEY)` scanning `/dev/input/event*` for `KEY_POWER` | Like MY355's `findPowerDevice()` |
| Device open flags | `O_RDONLY \| O_NONBLOCK` — **no grab** (loong_power owns it) | Jawaka `jw__open_power_key()` |
| Short press (< 2s) | Ignored; loong_power handles suspend/wake | loong_power owns this |
| Long press (>= 3s) | Display shutdown overlay, sync filesystems, call `poweroff` | SDLReader's `requestShutdown()` |
| Resume debounce | Ignore events for 500ms after wake from suspend | Like TG5040's `POST_RESUME_IGNORE_DURATION` |
| Screen blank on suspend | Leave to loong_power (it handles via `bl_power`) | loong_power does this |
| Battery status | Read `/sys/class/power_supply/battery/capacity` and `/sys/class/power_supply/ac/online` | Jawaka `jw__mlp1_get_status()` |
| Auto-sleep | Not needed — loong_power handles it via `loong_power.cfg` | Use stock behavior |

#### Suspend Path (loong_power SDK)

For cases where SDLReader wants to trigger suspend programmatically (e.g.,
menu option), prefer the loong SDK PowerApi:

```c
// dlopen /usr/lib/libloong_sdk.so
// Symbol: _ZN5loong8PowerApi3getEv  → PowerApi::get()
// Symbol: _ZN5loong8PowerApi12powerStandbyEv  → PowerApi::powerStandby()
void *api = s_power_get();
s_power_standby(api);
```

Fall back to `echo mem > /sys/power/state`.

**Critical**: Using loong's own standby keeps `loong_power` in sync so a
single power button press wakes the device. Raw `echo mem` causes loong to
re-suspend on the first wake press, requiring two presses to wake.

#### Key Constants

```cpp
// Power key search uses dynamic discovery (like MY355), not a fixed path
// Device path: dynamically found via EVIOCGBIT scanning

// Suspend methods (ordered by preference)
// 1. loong SDK PowerApi::powerStandby() — keeps loong_power in sync
// 2. echo mem > /sys/power/state — fallback, may need 2 presses to wake

// Shutdown
// /usr/sbin/poweroff

// Battery
// capacity:  /sys/class/power_supply/battery/capacity
// charging:  /sys/class/power_supply/ac/online (or usb/online)
```

#### Files to Create

| File | Purpose |
|------|---------|
| `ports/mlp1/include/power_handler.h` | PowerHandler header, MLP1 variant |
| `ports/mlp1/src/power_handler.cpp` | PowerHandler implementation, MLP1 variant |

Modeled on MY355's dynamic device discovery (`findPowerDevice()` via
`EVIOCGBIT`) but with read-only open (no grab) and no screen blank/bl_power
writes (loong_power handles that).

**Makefile integration**: The `ports/mlp1/Makefile` compiles the MLP1 power
handler and links it in (same pattern as MY355's `my355_power_handler.o`).

**Code integration**: SDLReader's `app.cpp` already has `#ifdef TRIMUI_PLATFORM`
conditional compilation for the PowerHandler. Add `#if defined(TRIMUI_PLATFORM) || defined(PLATFORM_MLP1)` to include the power handler, or add a separate
`#elif defined(PLATFORM_MLP1)` block.

---

## Phase 3: Leaf Deployment Infrastructure

### 3.1 Launch Script (`pak/launch.sh` — MLP1 variant)

Create a Leaf-compatible launch script that follows the same contract as
`Thing-File/pak/launch.sh` and `ssh-server/pak/launch.sh`. Key differences
from the existing NextUI `ports/trimui/pak-template/launch.sh`:

- Must resolve SD card root via `find_sdcard_root()` pattern
- Must source `env.sh` for runtime paths
- Sets `SDL_VIDEODRIVER=wayland` when `/var/run` exists (MLP1 uses Wayland)
- Sets `SDL_READER_DEFAULT_DIR` and `SDL_READER_STATE_DIR` to Leaf runtime paths
- No first-run docs.pdf logic (keep it simple; remove docs.pdf dependency)
- Uses `exec "$BIN" -b "$@"` to launch file browser

**File:** `ports/mlp1/pak/launch.sh` (new, distinct from `ports/trimui/pak-template/launch.sh`)

### 3.2 Package Build Target (`package-mlp1`)

Add a packaging target to the root `Makefile` that:

1. Builds the MLP1 binary (`make mlp1`)
2. Creates `build/mlp1/package/SDLReader.pak/` directory
3. Populates it with:
   - `bin/sdl_reader_cli` — the compiled binary
   - `launch.sh` — Leaf-compatible launch script (from `ports/mlp1/pak/`)
   - `pak.json` — minimal metadata (name, platform, version)
   - `fonts/` — bundled fonts (same as existing)
   - `res/` — resources (docs.pdf if desired)
   - `lib/` — only if runtime shared libraries are needed
4. Strips the binary
5. Bundles runtime libraries via `make_bundle.sh` (if any .so deps remain)

The output goes to `build/mlp1/package/SDLReader.pak/`.

### 3.3 ADB Staging (`adb-stage-pak-mlp1`)

Add a target for direct ADB push to a connected MLP1 device:

1. Build and package (`make package-mlp1`)
2. Detect ADB device
3. Resolve SD card path via `adb-resolve-umrk-sd.sh` pattern (or simpler:
   push to `/mnt/sdcard/Apps/mlp1/`)
4. Push `.pak/` directory to device
5. Set permissions

**File:** script in `ports/mlp1/scripts/adb-stage-pak.sh` or inline Makefile
target.

### 3.4 Leaf Registration (`app-package-policy.sh`)

Add SDLReader to `/Volumes/Storage/UMRK/Leaf/scripts/app-package-policy.sh`:

```bash
SDLReader)
    package_target="package-platform"
    package_platform="mlp1"
    package_dir="$workspace_dir/SDLReader-brick/build/mlp1/package/SDLReader.pak"
    package_name="SDLReader.pak"/
    destination_platform="mlp1"
    supported_devices="mlp1"
    ;;
```

Also add `SDLReader-brick` to `stage/common.mk`'s `ALL_REPOS` or as a
standalone entry. Since SDLReader-brick lives outside the `UMRK/` workspace
tree (it's at `/Volumes/Storage/GitHub/SDLReader-brick`), the `WORKSPACE_DIR`
mapping needs to be adjusted or a symlink placed at
`<UMRK_ROOT>/SDLReader-brick`.

**Recommended approach:** Add a symlink:
```bash
ln -s /Volumes/Storage/GitHub/SDLReader-brick /Volumes/Storage/UMRK/SDLReader-brick
```

Then update `common.mk` to define `SDLREADER_DIR` and add it to `ALL_REPOS`:

```makefile
SDLREADER_DIR ?= $(WORKSPACE_DIR)/SDLReader-brick
```

And update `ALL_REPOS` to include `SDLReader-brick`.

### 3.5 Leaf mlp1.mk Stage Target

Add SDLReader to `STAGE_APPS` in `/Volumes/Storage/UMRK/Leaf/stage/mlp1.mk`:

```makefile
STAGE_APPS ?= ssh-server Thing-File retroarch-builds SDLReader-brick
```

This makes `make stage DEVICE=mlp1` include SDLReader in the full device
provisioning.

---

## Phase 4: Verification & ADB Smoke Testing

### 4.1 Build Verification

```bash
# From SDLReader-brick root:
make mlp1

# Verify binary:
file build/mlp1/sdl_reader_cli
# Expected: ELF 64-bit LSB executable, ARM aarch64, dynamically linked
```

### 4.2 Package Verification

```bash
make package-mlp1
ls -la build/mlp1/package/SDLReader.pak/
# Expected: bin/ launch.sh pak.json fonts/ res/
```

### 4.3 ADB Deploy Smoke Test

```bash
# From SDLReader-brick root:
make adb-stage-pak-mlp1

# Verify on device:
adb shell "ls -la /mnt/sdcard/Apps/mlp1/SDLReader.pak/"

# Launch manually on device (via adb shell):
adb shell "export SDL_VIDEODRIVER=wayland && /mnt/sdcard/Apps/mlp1/SDLReader.pak/launch.sh"
```

### 4.4 Full Leaf Stage Test

```bash
# From Leaf root:
make stage-app APP=SDLReader-brick DEVICE=mlp1
```

### 4.5 Real-Device Validation Checklist

- [ ] Binary launches and shows the Nuklear file browser
- [ ] Touch/joystick D-pad navigation works in file browser
- [ ] A button selects a file, B button goes back
- [ ] PDF rendering works (page flip, zoom, fit-to-width)
- [ ] EPUB/MOBI rendering works
- [ ] CBZ/CBR comic archive rendering works
- [ ] Image viewer works
- [ ] Menu/settings accessible via Start button
- [ ] MirrorHorizontal (if mapped) toggles correctly
- [ ] Edge-turn navigation works
- [ ] Reading history persists across launches
- [ ] Config saves between sessions
- [ ] Font selection works
- [ ] Reading themes work (Dark Mode, Sepia, etc.)
- [ ] Performance is acceptable (page turns < 1s)
- [ ] No crashes when opening <Return to file browser> etc.

### 4.6 Known Risks

1. **Wayland vs X11 SDL backend**: SDLReader creates an SDL window with
   `SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE`. On MLP1, `SDL_VIDEODRIVER`
   is set to `wayland`. Need to verify this works correctly. If Wayland has
   issues, fall back to `KMSDRM` SDL video driver which works on the device
   framebuffer directly.

2. **Game controller mapping**: The Loong Gamepad may not have a built-in
   SDL game controller mapping. The MLP1 device overlay includes a mapping
   at `device/mlp1/autoconfig/Loong Gamepad.cfg`. Check if SDL2 on device
   is compiled with SDL2\_gamecontrollerdb support and has this mapping
   loaded.

3. **Touch input**: The MLP1 does have touch input. The Nuklear SDL renderer
   backend should handle touch events. If touch is desired, verify touch
   events are routed to Nuklear.

4. **Font rendering performance**: SDL\_ttf + Nuklear on 960x720 should be
   fine, but measure frame rate during menu rendering.

5. **Display rotation**: The physical panel is 720x960 portrait; Weston's
   `weston.ini` has `[output] name=DSI-1 transform=rotate-90` making the
   logical display 960x720 landscape. Confirmed working. SDL apps using the
   Wayland backend get the compositor-rotated coordinate space transparently.

---

## Phase 5: Configuration & Polish

### 5.1 Runtime Path Configuration

The launch script should set Leaf-aware defaults:

```bash
# Default library root matches Leaf SD card paths
export SDL_READER_DEFAULT_DIR="/mnt/sdcard"

# State directory matches Leaf userdata
export SDL_READER_STATE_DIR="${UMRK_APPS_DATA_PATH:-$HOME/.userdata/SDLReader}"
```

### 5.2 Platform-Specific Overrides

If real-device testing reveals issues, create `ports/mlp1/include/`
directory with:
- `platform_mlp1.h` — any MLP1-specific scancode or input quirks
- Input repeat calibration based on actual gamepad feel

### 5.3 Remove Pak Store Comments

Since the MLP1 artifact is not submitted to the NextUI Pak Store:
- `pak.json` stays **unchanged** (it still lists `["tg5040", "tg5050", "my355"]`)
- The MLP1 `.pak` gets its own `pak.json` at packaging time with:
  ```json
  { "name": "SDLReader", "icon": "", "platform": "mlp1",
    "pak_version": "2.4.0", "min_jawaka_version": "0.0.1" }
  ```

---

## File Inventory — New & Modified

### New Files

| File | Purpose |
|------|---------|
| `ports/mlp1/Makefile` | MLP1 cross-compile build (modeled on `my355/makefile`) |
| `ports/mlp1/include/power_handler.h` | MLP1 power handler header (read-only observer, no grab) |
| `ports/mlp1/src/power_handler.cpp` | MLP1 power handler impl (dynamic device scan, loong SDK suspend) |
| `ports/mlp1/pak/launch.sh` | Leaf-compatible launch script |
| `ports/mlp1/export_bundle.sh` | MLP1 bundle export (simple `.pak` staging) |
| `ports/mlp1/make_bundle.sh` | Library bundling |
| `ports/mlp1/scripts/adb-stage-pak.sh` | ADB staging helper |
| `ports/mlp1/.gitignore` | Ignore `nuklear-mlp1/`, `mupdf/`, `libwebp-*` |
| `plans/port-to-mlp1.md` | This plan |

### Modified Files

| File | Change |
|------|--------|
| `Makefile` | Add `mlp1`, `package-mlp1`, `export-mlp1`, `adb-stage-pak-mlp1` targets; add MLP1 to `deploy` fingerprint detection; add to `clean`, `list-platforms`, `help` |
| `cli/main.cpp` | Add `PLATFORM_MLP1` resolution (960x720) |
| `src/button_mapper.cpp` | Add `PLATFORM_MLP1` branch with standard mappings |
| `src/app.cpp` | Add `PLATFORM_MLP1` to power handler conditional (alongside `TRIMUI_PLATFORM`) |
| `include/button_mapper.h` | No change needed if the .cpp handles it |
| `include/platform_constants.h` | Add `PLATFORM_MLP1` constants (dead zone, repeat timing) |
| `include/file_browser.h` | Add `PLATFORM_MLP1` thumbnail size (120) |

### External Files (Leaf Workspace)

| File | Change |
|------|--------|
| `UMRK/Leaf/scripts/app-package-policy.sh` | Register `SDLReader` app |
| `UMRK/Leaf/stage/common.mk` | Add `SDLREADER_DIR` and `SDLReader-brick` to `ALL_REPOS` |
| `UMRK/Leaf/stage/mlp1.mk` | Add `SDLReader-brick` to `STAGE_APPS` |

---

## Implementation Order (Suggested)

```
Phase 1 (Build System)
  ├── 1.1 Create ports/mlp1/Makefile
  ├── 1.2 Create ports/mlp1/export_bundle.sh + make_bundle.sh
  └── 1.3 Update root Makefile with MLP1 targets

Phase 2 (Code Changes)
  ├── 2.1 Add MLP1 resolution (960x720) to cli/main.cpp
  ├── 2.2 Add MLP1 button mappings to src/button_mapper.cpp
  ├── 2.3 Add MLP1 constants to include/platform_constants.h
  ├── 2.4 Add MLP1 thumbnail dims to include/file_browser.h
  └── 2.6 Create ports/mlp1/include/power_handler.h + ports/mlp1/src/power_handler.cpp
      └── Update src/app.cpp to include MLP1 power handler

Phase 3 (Deployment)
  ├── 3.1 Create ports/mlp1/pak/launch.sh
  ├── 3.2 Add package-mlp1 target to Makefile
  ├── 3.3 Add adb-stage-pak-mlp1 target
  └── 3.4 Register in Leaf app-package-policy.sh + common.mk + mlp1.mk

Phase 4 (Testing)
  ├── 4.1 Build verification
  ├── 4.2 Package verification
  ├── 4.3 ADB deploy smoke test
  └── 4.4 Real-device validation

Phase 5 (Polish)
  ├── 5.1 Runtime path config in launch script
  ├── 5.2 Platform-specific overrides if needed
  └── 5.3 Verify pak.json is not touched
```
