#pragma once

#include <SDL.h>

inline constexpr SDL_JoystickID H700_INPUT_INSTANCE_ID = -700;

void initializeH700Input();
void pollH700Input();
void closeH700Input();
