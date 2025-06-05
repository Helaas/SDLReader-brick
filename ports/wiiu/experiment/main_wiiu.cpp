/*

*/

#ifndef HAS_DKO_SDL_QUIT_FIXES
#include <sysapp/title.h> // SYSCheckTitleExists()
#endif

#include "app.h"
#include "renderer.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <cstdio>
#include <stdexcept>

// --- Cleanup Function ---
void cleanupSDL(SDL_Window *window, SDL_Renderer *renderer)
{
    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
    }
    if (window)
    {
        SDL_DestroyWindow(window);
    }
    TTF_Quit();
    SDL_Quit();
}

int main(int argc, char **argv)
{

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    int returnCode = 0;

    Uint32 sdl_flags = Renderer::getRequiredSDLInitFlags();

    if (SDL_Init(sdl_flags) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s", SDL_GetError());
        cleanupSDL(window, renderer);
        return 1;
    }
    if (TTF_Init() == -1)
    {
        printf("SDL_ttf could not initialize! TTF_Error: %s", TTF_GetError());
        cleanupSDL(window, renderer);
        return 1;
    }

    window = SDL_CreateWindow(
        "SDLReader C++",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        800,
        600,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        printf("Window could not be created! SDL_Error: %s", SDL_GetError());
        cleanupSDL(window, renderer);
        return 1;
    }
    printf("SDL_Window created.");
    renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        printf("Renderer could not be created! SDL_Error: %s", SDL_GetError());
        cleanupSDL(window, renderer);
        return 1;
    }
    printf("SDL_Renderer created.");

    // --- Application Run Phase ---
    try
    {
        App app(argv[1], window, renderer);
        app.run();
    }
    catch (const std::runtime_error &e)
    {
        printf("Application Error: %s", e.what());
        returnCode = 1;
    }
    catch (const std::exception &e)
    {
        printf("An unexpected error occurred: %s", e.what());
        returnCode = 1;
    }

    // --- Cleanup Phase ---
    cleanupSDL(window, renderer);

#ifndef HAS_DKO_SDL_QUIT_FIXES
    SYSCheckTitleExists(0); // workaround for SDL bug
#endif
}
