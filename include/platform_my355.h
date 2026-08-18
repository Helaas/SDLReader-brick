#pragma once

#include <SDL.h>

// MY355 (Miyoo Flip) sends all button presses as both keyboard HID scancodes
// AND joystick/controller events. This filter identifies keyboard events that
// originate from physical buttons so they can be suppressed, preventing
// double-processing. The controller events are kept as the primary input path,
// matching the same behavior as TG5040/TG5050 (Smart Pro).
static inline bool isMy355ButtonScancode(SDL_Scancode sc)
{
    switch (sc)
    {
    case SDL_SCANCODE_SPACE:     // A button
    case SDL_SCANCODE_LCTRL:     // B button
    case SDL_SCANCODE_LSHIFT:    // X button
    case SDL_SCANCODE_LALT:      // Y button
    case SDL_SCANCODE_UP:        // D-pad Up
    case SDL_SCANCODE_DOWN:      // D-pad Down
    case SDL_SCANCODE_LEFT:      // D-pad Left
    case SDL_SCANCODE_RIGHT:     // D-pad Right
    case SDL_SCANCODE_RETURN:    // START
    case SDL_SCANCODE_RCTRL:     // SELECT
    case SDL_SCANCODE_TAB:       // L1
    case SDL_SCANCODE_BACKSLASH: // R1
    case SDL_SCANCODE_PAGEUP:    // L2
    case SDL_SCANCODE_PAGEDOWN:  // R2
    case SDL_SCANCODE_ESCAPE:    // MENU
        return true;
    default:
        return false;
    }
}
