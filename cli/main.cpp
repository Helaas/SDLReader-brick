#include "app.h"
#include "file_browser.h"
#include "options_manager.h"
#include "path_utils.h"
#include "renderer.h"
#include "supported_file_types.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <iostream>
#include <memory>

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

#ifdef PLATFORM_MY355
    constexpr int kDefaultWindowWidth = 640;
    constexpr int kDefaultWindowHeight = 480;
#elif defined(TRIMUI_PLATFORM)
    constexpr int kDefaultWindowWidth = 800;
    constexpr int kDefaultWindowHeight = 600;
#else
    // Desktop builds start at 2x the TG5040's 4:3 layout so the thumbnail grid
    constexpr int kDefaultWindowWidth = 1280;
    constexpr int kDefaultWindowHeight = 960;
#endif

    bool browseMode = false;
    bool forceShowImagesInFileBrowser = false;
    bool startFileBrowserInThumbnailView = false;
    std::string documentPath;
    auto printUsage = [argv]()
    {
        std::cerr << "Usage: " << argv[0] << " <document_file>" << std::endl;
        std::cerr << "       " << argv[0] << " --browse [--show-filebrowser-images] [--filebrowser-thumbnail-view]" << std::endl;
        std::cerr << "Supported formats: " << SupportedFileTypes::getSupportedFormatHelpText() << std::endl;
    };

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--browse" || arg == "-b")
        {
            browseMode = true;
        }
        else if (arg == "--show-filebrowser-images")
        {
            forceShowImagesInFileBrowser = true;
        }
        else if (arg == "--filebrowser-thumbnail-view")
        {
            startFileBrowserInThumbnailView = true;
        }
        else if (!arg.empty() && arg.front() == '-')
        {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage();
            return 1;
        }
        else if (documentPath.empty())
        {
            documentPath = arg;
        }
        else
        {
            std::cerr << "Only one document path may be provided." << std::endl;
            printUsage();
            return 1;
        }
    }

    std::string browseOnlyOptions;
    auto appendBrowseOnlyOption = [&browseOnlyOptions](const char* option)
    {
        if (!browseOnlyOptions.empty())
        {
            browseOnlyOptions += ", ";
        }
        browseOnlyOptions += option;
    };
    if (forceShowImagesInFileBrowser)
    {
        appendBrowseOnlyOption("--show-filebrowser-images");
    }
    if (startFileBrowserInThumbnailView)
    {
        appendBrowseOnlyOption("--filebrowser-thumbnail-view");
    }

    if (!browseMode && !browseOnlyOptions.empty())
    {
        std::cerr << "The following options require --browse: " << browseOnlyOptions << std::endl;
        printUsage();
        return 1;
    }

    if (browseMode && !documentPath.empty())
    {
        std::cerr << "A document path cannot be combined with --browse." << std::endl;
        printUsage();
        return 1;
    }

    if (!browseMode && documentPath.empty())
    {
        printUsage();
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
        kDefaultWindowWidth,
        kDefaultWindowHeight,
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

    // Main loop: If browse mode, keep returning to file browser after closing document
    // Use unique_ptr to avoid stack overflow - FileBrowser has large members
    auto browser = std::make_unique<FileBrowser>();
    bool continueRunning = true;
    while (continueRunning)
    {
        std::cout << "Main: Loop iteration - browseMode=" << browseMode
                  << ", documentPath=" << (documentPath.empty() ? "empty" : documentPath) << std::endl;

        // If browse mode or no document path, run file browser
        if (browseMode || documentPath.empty())
        {
            std::cout << "Starting file browser..." << std::endl;

            // Load config to get last browse directory
            OptionsManager optionsManager;
            FontConfig config = optionsManager.loadConfig();
            std::string startPath = config.lastBrowseDirectory.empty() ? getDefaultLibraryRoot() : config.lastBrowseDirectory;
            FileBrowserLaunchOptions browserLaunchOptions;
            browserLaunchOptions.showImagesInFileBrowser = config.showImagesInFileBrowser || forceShowImagesInFileBrowser;
            browserLaunchOptions.startInThumbnailView = startFileBrowserInThumbnailView;

            if (!browser->initialize(window, renderer, startPath, browserLaunchOptions))
            {
                std::cerr << "Failed to initialize file browser" << std::endl;
                cleanupSDL(window, renderer);
                return 1;
            }

            // Run browser and get selected file (cleanup happens automatically inside run())
            documentPath = browser->run();

            // Save last browsed directory back to config
            std::string lastDir = browser->getLastDirectory();
            if (!lastDir.empty())
            {
                config.lastBrowseDirectory = lastDir;
                optionsManager.saveConfig(config);
            }

            // If user cancelled (empty path), exit
            if (documentPath.empty())
            {
                std::cout << "No file selected, exiting." << std::endl;
                continueRunning = false;
                continue;
            }

            std::cout << "Selected file: " << documentPath << std::endl;
        }

        // Now open the document
        std::cout << "Main: Opening document: " << documentPath << std::endl;
        std::cout.flush();
        try
        {
            App app(documentPath, window, renderer, AppLaunchOptions{forceShowImagesInFileBrowser});
            std::cout << "Main: App instance created, calling run()" << std::endl;
            std::cout.flush();
            app.run();
        }
        catch (const std::runtime_error& e)
        {
            std::cerr << "Application Error: " << e.what() << std::endl;
            std::cerr.flush();
            returnCode = 1;
        }
        catch (const std::exception& e)
        {
            std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
            std::cerr.flush();
            returnCode = 1;
        }
        catch (...)
        {
            std::cerr << "Unknown exception caught!" << std::endl;
            std::cerr.flush();
            returnCode = 1;
        }

        // After app closes, if not in browse mode, exit the loop
        if (!browseMode)
        {
            continueRunning = false;
        }
        else
        {
            // Clear document path to force browser to show on next iteration
            std::cout << "Main: Browse mode active, returning to file browser" << std::endl;
            documentPath.clear();
        }
    }

    cleanupSDL(window, renderer);

    return returnCode;
}
