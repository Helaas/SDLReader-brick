#pragma once

#include <SDL.h>
#include "platform_runtime.h"

namespace PlatformConstants
{

// Analog stick dead zone
// Used by: App, InputManager, GuiManager, FileBrowser
inline const Sint16 AXIS_DEAD_ZONE = isMy355Platform() ? 20000 : 8000;

// Held-input repeat timing (D-pad / analog stick continuous scrolling/nav)
// Used by: GuiManager (settings nav), FileBrowser (file list scrolling)
inline const Uint32 INPUT_INITIAL_DELAY_MS = isMy355Platform() ? 220 : 100;
inline const Uint32 INPUT_REPEAT_DELAY_MS = isMy355Platform() ? 130 : 50;

// Page jump digit entry timeout
inline constexpr Uint32 PAGE_JUMP_TIMEOUT = 5000;

// Expensive render detection threshold
inline constexpr Uint32 EXPENSIVE_RENDER_THRESHOLD_MS = 200;

// Default assumed render duration before any measurement
inline constexpr Uint32 DEFAULT_RENDER_DURATION_MS = 300;

} // namespace PlatformConstants
