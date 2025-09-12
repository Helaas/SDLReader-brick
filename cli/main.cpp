#include "app.h"
#include "renderer.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <iostream>

void cleanupSDL(SDL_Window* window, SDL_Renderer* renderer) {
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    TTF_Quit();
    SDL_Quit();
}

int main(int argc, char *argv[])
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    int returnCode = 0;

    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <document_file>" << std::endl;
        std::cerr << "Supported formats: PDF (.pdf), Comic Book Archives (.cbz, .cbr, .rar, .zip), EPUB (.epub), MOBI (.mobi)" << std::endl;
        return 1;
    }

    Uint32 sdl_flags = Renderer::getRequiredSDLInitFlags();
    if (SDL_Init(sdl_flags) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        cleanupSDL(window, renderer);
        return 1;
    }

    if (TTF_Init() == -1) {
        std::cerr << "SDL_ttf could not initialize! TTF_Error: " << TTF_GetError() << std::endl;
        cleanupSDL(window, renderer);
        return 1;
    }

    window = SDL_CreateWindow(
        "SDLReader C++",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        800,
        600,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        cleanupSDL(window, renderer);
        return 1;
    }

    renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        cleanupSDL(window, renderer);
        return 1;
    }

    try
    {
        App app(argv[1], window, renderer);
        app.run();
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "Application Error: " << e.what() << std::endl;
        returnCode = 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
        returnCode = 1;
    }

    cleanupSDL(window, renderer);

    return returnCode;
}