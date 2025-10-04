#include "app.h"
#include "file_browser.h"
#include "options_manager.h"
#include "renderer.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <cstring>
#include <iostream>

void cleanupSDL(SDL_Window* window, SDL_Renderer* renderer)
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

int main(int argc, char* argv[])
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    int returnCode = 0;

    // Check for --browse flag
    bool browseMode = false;
    std::string documentPath;

    if (argc == 2)
    {
        if (std::strcmp(argv[1], "--browse") == 0 || std::strcmp(argv[1], "-b") == 0)
        {
            browseMode = true;
        }
        else
        {
            documentPath = argv[1];
        }
    }
    else if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <document_file>" << std::endl;
        std::cerr << "       " << argv[0] << " --browse" << std::endl;
        std::cerr << "Supported formats: PDF (.pdf), Comic Book Archives (.cbz, .cbr, .rar, .zip), EPUB (.epub), MOBI (.mobi)" << std::endl;
        return 1;
    }

    Uint32 sdl_flags = Renderer::getRequiredSDLInitFlags();
    if (SDL_Init(sdl_flags) < 0)
    {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        cleanupSDL(window, renderer);
        return 1;
    }

    if (TTF_Init() == -1)
    {
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
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        cleanupSDL(window, renderer);
        return 1;
    }

    renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        cleanupSDL(window, renderer);
        return 1;
    }

    // If browse mode, run file browser first
    if (browseMode)
    {
        std::cout << "Starting file browser..." << std::endl;

        // Load config to get last browse directory
        OptionsManager optionsManager;
        FontConfig config = optionsManager.loadConfig();
#ifdef TG5040_PLATFORM
        std::string defaultPath = "/mnt/SDCARD";
#else
        const char* home = getenv("HOME");
        std::string defaultPath = home ? home : "/";
#endif
        std::string startPath = config.lastBrowseDirectory.empty() ? defaultPath : config.lastBrowseDirectory;

        FileBrowser browser;
        if (!browser.initialize(window, renderer, startPath))
        {
            std::cerr << "Failed to initialize file browser" << std::endl;
            cleanupSDL(window, renderer);
            return 1;
        }

        // Run browser and get selected file
        documentPath = browser.run();

        // Save last browsed directory back to config
        std::string lastDir = browser.getLastDirectory();
        if (!lastDir.empty())
        {
            config.lastBrowseDirectory = lastDir;
            optionsManager.saveConfig(config);
        }

        browser.cleanup();

        // If user cancelled (empty path), exit
        if (documentPath.empty())
        {
            std::cout << "No file selected, exiting." << std::endl;
            cleanupSDL(window, renderer);
            return 0;
        }

        std::cout << "Selected file: " << documentPath << std::endl;
    }

    // Now open the document
    try
    {
        App app(documentPath, window, renderer);
        app.run();
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "Application Error: " << e.what() << std::endl;
        returnCode = 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
        returnCode = 1;
    }

    cleanupSDL(window, renderer);

    return returnCode;
}
