#pragma once

#include <SDL.h>
#include "platform_runtime.h"

namespace PlatformConstants
{

// Analog stick dead zone
// Used by: App, InputManager, GuiManager, FileBrowser
// MLP1 is built as its own binary, so it stays a compile-time branch; the NextUI
// universal binary picks its values at runtime from $PLATFORM.
#ifdef PLATFORM_MLP1
inline constexpr Sint16 AXIS_DEAD_ZONE = 20000;
#else
inline const Sint16 AXIS_DEAD_ZONE = isMy355Platform() ? 20000 : 8000;
#endif

// Held-input repeat timing (D-pad / analog stick continuous scrolling/nav)
// Used by: GuiManager (settings nav), FileBrowser (file list scrolling)
#ifdef PLATFORM_MLP1
inline constexpr Uint32 INPUT_INITIAL_DELAY_MS = 220;
inline constexpr Uint32 INPUT_REPEAT_DELAY_MS = 130;
#else
inline const Uint32 INPUT_INITIAL_DELAY_MS = isMy355Platform() ? 220 : 100;
inline const Uint32 INPUT_REPEAT_DELAY_MS = isMy355Platform() ? 130 : 50;
#endif

// Page jump digit entry timeout
inline constexpr Uint32 PAGE_JUMP_TIMEOUT = 5000;

// Expensive render detection threshold
inline constexpr Uint32 EXPENSIVE_RENDER_THRESHOLD_MS = 200;

// Default assumed render duration before any measurement
inline constexpr Uint32 DEFAULT_RENDER_DURATION_MS = 300;

} // namespace PlatformConstants
