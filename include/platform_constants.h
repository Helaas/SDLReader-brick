#pragma once

#include <SDL.h>

namespace PlatformConstants
{

// Analog stick dead zone
// Used by: App, InputManager, GuiManager, FileBrowser
#ifdef PLATFORM_MY355
inline constexpr Sint16 AXIS_DEAD_ZONE = 20000;
#else
inline constexpr Sint16 AXIS_DEAD_ZONE = 8000;
#endif

// Held-input repeat timing (D-pad / analog stick continuous scrolling/nav)
// Used by: GuiManager (settings nav), FileBrowser (file list scrolling)
#ifdef PLATFORM_MY355
inline constexpr Uint32 INPUT_INITIAL_DELAY_MS = 220;
inline constexpr Uint32 INPUT_REPEAT_DELAY_MS = 130;
#else
inline constexpr Uint32 INPUT_INITIAL_DELAY_MS = 100;
inline constexpr Uint32 INPUT_REPEAT_DELAY_MS = 50;
#endif

// Page jump digit entry timeout
inline constexpr Uint32 PAGE_JUMP_TIMEOUT = 5000;

// Expensive render detection threshold
inline constexpr Uint32 EXPENSIVE_RENDER_THRESHOLD_MS = 200;

// Default assumed render duration before any measurement
inline constexpr Uint32 DEFAULT_RENDER_DURATION_MS = 300;

} // namespace PlatformConstants
