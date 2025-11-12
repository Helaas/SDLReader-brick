#pragma once

#include <SDL.h>

// Returns the SDL event type reserved for power/shutdown messages.
// Registered lazily the first time it is requested after SDL is initialized.
inline Uint32 getPowerMessageEventType()
{
    static Uint32 eventType = []()
    {
        Uint32 type = SDL_RegisterEvents(1);
        if (type == static_cast<Uint32>(-1))
        {
            // SDL is out of custom event slots; fall back to generic user event.
            return static_cast<Uint32>(SDL_USEREVENT);
        }
        return type;
    }();
    return eventType;
}
