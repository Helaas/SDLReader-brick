#include "file_browser.h"
#include <imgui.h>

#ifdef TG5040_PLATFORM
#include "power_handler.h"
#include <imgui_impl_sdl.h>         // TG5040 uses v1.85 headers
#include <imgui_impl_sdlrenderer.h> // Compatible with SDL 2.0.9
#else
#include <imgui_impl_sdl2.h> // Modern platforms use v1.89+ headers
#include <imgui_impl_sdlrenderer2.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <sys/stat.h>

#ifdef TG5040_PLATFORM
#define DEFAULT_START_PATH "/mnt/SDCARD"
#else
#include <pwd.h>
#include <unistd.h>
#endif

FileBrowser::FileBrowser()
    : m_window(nullptr), m_renderer(nullptr), m_initialized(false), m_running(false)
#ifdef TG5040_PLATFORM
      ,
      m_currentPath(DEFAULT_START_PATH)
#else
      ,
      m_currentPath(getenv("HOME") ? getenv("HOME") : "/")
#endif
      ,
      m_selectedIndex(0), m_gameController(nullptr), m_gameControllerInstanceID(-1)
{
}

FileBrowser::~FileBrowser()
{
    cleanup();
}

bool FileBrowser::initialize(SDL_Window* window, SDL_Renderer* renderer, const std::string& startPath)
{
    m_window = window;
    m_renderer = renderer;
    m_currentPath = startPath;

    // Setup Dear ImGui context (or reuse existing one in browse mode)
    bool isNewContext = (ImGui::GetCurrentContext() == nullptr);
    if (isNewContext)
    {
        std::cout << "FileBrowser: Creating new ImGui context" << std::endl;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
    }
    else
    {
        std::cout << "FileBrowser: Reusing existing ImGui context" << std::endl;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    // Reset config flags for file browser
    io.ConfigFlags = 0; // Clear all flags first
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad); // rely on bespoke navigation to avoid ImGui's multi-highlight state

    // Reset the style colors
    ImGui::StyleColorsDark();

#ifdef TG5040_PLATFORM
    if (isNewContext)
    {
        // Scale widget sizes to 3x for TG5040 (640x480 display)
        // This must match GuiManager's scaling
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(3.0f);
        std::cout << "FileBrowser: Applied initial 3x widget scale for TG5040" << std::endl;
    }
    // Font scale is independent of widget scale - FileBrowser uses 3x for fonts
    io.FontGlobalScale = 3.0f;
    std::cout << "FileBrowser: Set FontGlobalScale to 3.0" << std::endl;
#endif

    // Setup Platform/Renderer backends
#ifdef TG5040_PLATFORM
    if (!ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer))
    {
        std::cerr << "Failed to initialize ImGui SDL2 backend" << std::endl;
        return false;
    }

    if (!ImGui_ImplSDLRenderer_Init(m_renderer))
    {
        std::cerr << "Failed to initialize ImGui SDL Renderer backend" << std::endl;
        return false;
    }
#else
    if (!ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer))
    {
        std::cerr << "Failed to initialize ImGui SDL2 backend" << std::endl;
        return false;
    }

    if (!ImGui_ImplSDLRenderer2_Init(m_renderer))
    {
        std::cerr << "Failed to initialize ImGui SDL Renderer backend" << std::endl;
        return false;
    }
#endif

#ifdef TG5040_PLATFORM
    // Initialize power handler
    m_powerHandler = std::make_unique<PowerHandler>();

    // Register sleep mode callback for fake sleep functionality
    m_powerHandler->setSleepModeCallback([this](bool enterFakeSleep)
                                         {
        m_inFakeSleep = enterFakeSleep;
        if (enterFakeSleep) {
            std::cout << "FileBrowser: Entering fake sleep mode - disabling inputs, screen will go black" << std::endl;
        } else {
            std::cout << "FileBrowser: Exiting fake sleep mode - re-enabling inputs and screen" << std::endl;
        } });

    // Register pre-sleep callback - file browser has no UI windows to close
    m_powerHandler->setPreSleepCallback([this]() -> bool
                                        {
                                            std::cout << "FileBrowser: Pre-sleep callback - no UI windows to close" << std::endl;
                                            return false; // No UI windows were closed
                                        });
#endif

    // Initialize game controllers
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        if (SDL_IsGameController(i))
        {
            m_gameController = SDL_GameControllerOpen(i);
            if (m_gameController)
            {
                m_gameControllerInstanceID = SDL_JoystickGetDeviceInstanceID(i);
                std::cout << "FileBrowser: Opened game controller: " << SDL_GameControllerName(m_gameController) << std::endl;
                break;
            }
            else
            {
                std::cerr << "FileBrowser: Could not open game controller: " << SDL_GetError() << std::endl;
            }
        }
    }

    // Scan initial directory
    if (!scanDirectory(m_currentPath))
    {
        std::cerr << "Failed to scan initial directory: " << m_currentPath << std::endl;
#ifdef TG5040_PLATFORM
        // Try fallback to /mnt/SDCARD
        if (m_currentPath != "/mnt/SDCARD")
        {
            m_currentPath = "/mnt/SDCARD";
            if (!scanDirectory(m_currentPath))
            {
                std::cerr << "Failed to scan fallback directory" << std::endl;
                return false;
            }
        }
        else
        {
            return false;
        }
#else
        // Try fallback to home directory or root
        const char* home = getenv("HOME");
        std::string fallbackPath = home ? home : "/";
        if (m_currentPath != fallbackPath)
        {
            m_currentPath = fallbackPath;
            if (!scanDirectory(m_currentPath))
            {
                std::cerr << "Failed to scan fallback directory" << std::endl;
                return false;
            }
        }
        else
        {
            return false;
        }
#endif
    }

    m_initialized = true;
    return true;
}

void FileBrowser::cleanup()
{
#ifdef TG5040_PLATFORM
    // Stop power handler first
    if (m_powerHandler)
    {
        m_powerHandler->stop();
    }
#endif

    if (m_initialized)
    {
        std::cout << "FileBrowser::cleanup(): Shutting down ImGui backends..." << std::endl;
#ifdef TG5040_PLATFORM
        ImGui_ImplSDLRenderer_Shutdown();
        ImGui_ImplSDL2_Shutdown();
#else
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
#endif
        // NOTE: We do NOT call ImGui::DestroyContext() here because:
        // 1. In browse mode, the App (GuiManager) will need to create a new ImGui context after this
        // 2. Destroying and recreating contexts can cause issues with SDL/ImGui state
        // 3. The context will be cleaned up when the program actually exits
        std::cout << "FileBrowser::cleanup(): Backends shutdown complete (context preserved)" << std::endl;
        m_initialized = false;
    }

    // Close game controller
    if (m_gameController)
    {
        SDL_GameControllerClose(m_gameController);
        m_gameController = nullptr;
        m_gameControllerInstanceID = -1;
        std::cout << "FileBrowser: Closed game controller" << std::endl;
    }
}

bool FileBrowser::scanDirectory(const std::string& path)
{
    // Make a defensive copy to avoid reference corruption issues
    const std::string safePath(path);
    std::cout << "scanDirectory: Scanning '" << safePath << "'" << std::endl;

    m_entries.clear();
    m_selectedIndex = 0;

    DIR* dir = opendir(safePath.c_str());
    if (!dir)
    {
        std::cerr << "Failed to open directory: " << safePath << " - " << strerror(errno) << std::endl;
        return false;
    }

    std::vector<FileEntry> directories;
    std::vector<FileEntry> files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;

        // Skip current directory
        if (name == ".")
        {
            continue;
        }

        // Skip other hidden files (starting with .) except ..
        if (name[0] == '.' && name != "..")
        {
            continue;
        }

        // Skip .. if we're at the base directory
#ifdef TG5040_PLATFORM
        if (name == ".." && (safePath == "/mnt/SDCARD" || safePath == "/"))
        {
            continue;
        }
#else
        if (name == ".." && safePath == "/")
        {
            continue;
        }
#endif

        std::string fullPath = safePath;
        if (!fullPath.empty() && fullPath.back() != '/')
        {
            fullPath += "/";
        }
        fullPath += name;

        if (name == "..")
        {
            std::string trimmed = safePath;
            while (trimmed.size() > 1 && trimmed.back() == '/')
            {
                trimmed.pop_back();
            }

            if (trimmed.empty() || trimmed == "/")
            {
                fullPath = "/";
            }
            else
            {
                size_t lastSlash = trimmed.find_last_of('/');
                if (lastSlash == std::string::npos || lastSlash == 0)
                {
                    fullPath = "/";
                }
                else
                {
                    fullPath = trimmed.substr(0, lastSlash);
                }
            }
        }

        struct stat statbuf;
        if (stat(fullPath.c_str(), &statbuf) == 0)
        {
            if (S_ISDIR(statbuf.st_mode))
            {
                directories.emplace_back(name, fullPath, true);
            }
            else if (S_ISREG(statbuf.st_mode) && isSupportedFile(name))
            {
                files.emplace_back(name, fullPath, false);
            }
        }
    }

    closedir(dir);

    // Sort directories and files separately (case-insensitive)
    auto caseInsensitiveCompare = [](const FileEntry& a, const FileEntry& b)
    {
        std::string aLower = a.name;
        std::string bLower = b.name;
        std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
        std::transform(bLower.begin(), bLower.end(), bLower.begin(), ::tolower);
        return aLower < bLower;
    };

    std::sort(directories.begin(), directories.end(), caseInsensitiveCompare);
    std::sort(files.begin(), files.end(), caseInsensitiveCompare);

    // Combine: directories first, then files
    m_entries.insert(m_entries.end(), directories.begin(), directories.end());
    m_entries.insert(m_entries.end(), files.begin(), files.end());

    std::cout << "scanDirectory: Found " << directories.size() << " directories and "
              << files.size() << " files (" << m_entries.size() << " total)" << std::endl;

    return true;
}

bool FileBrowser::isSupportedFile(const std::string& filename) const
{
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    return (lower.size() >= 4 &&
            (lower.substr(lower.size() - 4) == ".pdf" ||
             lower.substr(lower.size() - 4) == ".cbz" ||
             lower.substr(lower.size() - 4) == ".cbr" ||
             lower.substr(lower.size() - 4) == ".rar" ||
             lower.substr(lower.size() - 4) == ".zip")) ||
           (lower.size() >= 5 &&
            (lower.substr(lower.size() - 5) == ".epub" ||
             lower.substr(lower.size() - 5) == ".mobi"));
}

std::string FileBrowser::run()
{
    m_running = true;
    m_selectedFile.clear();

#ifdef TG5040_PLATFORM
    // Start power button monitoring
    if (m_powerHandler && !m_powerHandler->start())
    {
        std::cerr << "FileBrowser: Warning: Failed to start power button monitoring" << std::endl;
    }
#endif

    while (m_running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
#ifdef TG5040_PLATFORM
            // In fake sleep mode, ignore all SDL events (power button is handled by PowerHandler)
            if (!m_inFakeSleep)
            {
                handleEvent(event);
            }
            else
            {
                // Only handle quit events to allow graceful shutdown
                if (event.type == SDL_QUIT)
                {
                    handleEvent(event);
                }
            }
#else
            handleEvent(event);
#endif
        }

#ifdef TG5040_PLATFORM
        if (m_inFakeSleep)
        {
            // Fake sleep mode - render black screen
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
            SDL_RenderClear(m_renderer);
            SDL_RenderPresent(m_renderer);
        }
        else
        {
            render();
        }
#else
        render();
#endif

        // Small delay to prevent CPU spinning
        SDL_Delay(10);
    }

    std::cout << "FileBrowser: Cleaning up ImGui..." << std::endl;

    // Clear ImGui IO state before cleanup to prevent state bleeding
    if (m_initialized)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.WantCaptureKeyboard = false;
        io.WantCaptureMouse = false;
        io.NavActive = false;
        io.NavVisible = false;
        std::cout << "FileBrowser: Cleared ImGui IO state" << std::endl;
    }

    // Cleanup ImGui immediately so it doesn't interfere with main app
    cleanup();

    // Flush any remaining SDL events to prevent stale input
    SDL_Event event;
    int flushedEvents = 0;
    while (SDL_PollEvent(&event))
    {
        flushedEvents++;
        if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP)
        {
            std::cout << "Flushed controller event: type=" << event.type
                      << " button=" << (int) event.cbutton.button << std::endl;
        }
    }
    std::cout << "Flushed " << flushedEvents << " events from queue" << std::endl;

    // Small delay to ensure everything is processed
    SDL_Delay(100);

    std::cout << "FileBrowser: Cleanup complete, returning to app" << std::endl;

    return m_selectedFile;
}

void FileBrowser::render()
{
    // Start the Dear ImGui frame
#ifdef TG5040_PLATFORM
    ImGui_ImplSDLRenderer_NewFrame();
    ImGui_ImplSDL2_NewFrame();
#else
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
#endif
    ImGui::NewFrame();

    // Get window size
    int windowWidth, windowHeight;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);

    // Create a fullscreen window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
#ifdef TG5040_PLATFORM
    // Reduce size to account for scaled borders/padding/margins (3x scale)
    // This prevents scrollbar from appearing
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth - 1), static_cast<float>(windowHeight - 1)));
#else
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight)));
#endif

    ImGui::Begin("SDLReader###File Browser", nullptr,
                 ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

    // Display current path
    ImGui::TextWrapped("Current Directory: %s", m_currentPath.c_str());
    ImGui::Separator();

    // Instructions
#ifdef TG5040_PLATFORM
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "D-Pad: Nav. | A: Select | B: Back | Menu: Quit");
#else
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Arrow Keys/D-Pad: Navigate | Enter/A: Select | Backspace/B: Back | Escape: Quit");
#endif
    ImGui::Separator();

    // File list
    ImGui::BeginChild("FileList", ImVec2(0, -40), true);

    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const FileEntry& entry = m_entries[i];
        bool isSelected = (static_cast<int>(i) == m_selectedIndex);

        std::string displayName = entry.name;
        if (entry.isDirectory)
        {
            displayName = "[DIR] " + displayName;
        }

        if (ImGui::Selectable(displayName.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            m_selectedIndex = static_cast<int>(i);

            // Handle double-click
            if (ImGui::IsMouseDoubleClicked(0))
            {
                navigateInto();
            }
        }

        // Auto-scroll to selected item
        if (isSelected)
        {
            ImGui::SetScrollHereY(0.5f);
        }
    }

    ImGui::EndChild();

    // Status bar
    ImGui::Separator();
    if (m_entries.empty())
    {
        ImGui::Text("No files or directories found");
    }
    else
    {
        ImGui::Text("%zu items (%d directories, %d files)",
                    m_entries.size(),
                    static_cast<int>(std::count_if(m_entries.begin(), m_entries.end(),
                                                   [](const FileEntry& e)
                                                   { return e.isDirectory; })),
                    static_cast<int>(std::count_if(m_entries.begin(), m_entries.end(),
                                                   [](const FileEntry& e)
                                                   { return !e.isDirectory; })));
    }

    ImGui::End();

    // Rendering
    ImGui::Render();
    SDL_SetRenderDrawColor(m_renderer, 30, 30, 30, 255);
    SDL_RenderClear(m_renderer);
#ifdef TG5040_PLATFORM
    ImGui_ImplSDLRenderer_RenderDrawData(ImGui::GetDrawData());
#else
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);
#endif
    SDL_RenderPresent(m_renderer);
}

void FileBrowser::handleEvent(const SDL_Event& event)
{
    // Let ImGui process the event first
#ifdef TG5040_PLATFORM
    ImGui_ImplSDL2_ProcessEvent(&event);
#else
    ImGui_ImplSDL2_ProcessEvent(&event);
#endif

    // Check if ImGui wants to capture this event
    ImGuiIO& io = ImGui::GetIO();
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
    {
        if (io.WantCaptureKeyboard)
        {
            return; // ImGui is handling keyboard input
        }
    }
    if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEMOTION)
    {
        if (io.WantCaptureMouse)
        {
            return; // ImGui is handling mouse input
        }
    }

    switch (event.type)
    {
    case SDL_QUIT:
        m_running = false;
        break;

    case SDL_KEYDOWN:
        switch (event.key.keysym.sym)
        {
        case SDLK_ESCAPE:
        case SDLK_q:
            m_running = false;
            break;

        case SDLK_UP:
            if (!m_entries.empty())
            {
                const int total = static_cast<int>(m_entries.size());
                m_selectedIndex = (m_selectedIndex - 1 + total) % total;
            }
            break;

        case SDLK_DOWN:
            if (!m_entries.empty())
            {
                const int total = static_cast<int>(m_entries.size());
                m_selectedIndex = (m_selectedIndex + 1) % total;
            }
            break;

        case SDLK_RETURN:
        case SDLK_SPACE:
            navigateInto();
            break;

        case SDLK_BACKSPACE:
            navigateUp();
            break;

        default:
            break;
        }
        break;

    case SDL_CONTROLLERBUTTONDOWN:
        switch (event.cbutton.button)
        {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            if (!m_entries.empty())
            {
                const int total = static_cast<int>(m_entries.size());
                m_selectedIndex = (m_selectedIndex - 1 + total) % total;
            }
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (!m_entries.empty())
            {
                const int total = static_cast<int>(m_entries.size());
                m_selectedIndex = (m_selectedIndex + 1) % total;
            }
            break;

#ifdef TG5040_PLATFORM
        // On TG5040: Physical A button sends SDL_CONTROLLER_BUTTON_B
        // Physical B button sends SDL_CONTROLLER_BUTTON_A
        // (ImGui patch only affects ImGui's internal gamepad API, not raw SDL events we receive here)
        case SDL_CONTROLLER_BUTTON_B: // Physical A button -> Select/Enter
            std::cout << "Button B pressed (Physical A) - navigating into" << std::endl;
            navigateInto();
            break;

        case SDL_CONTROLLER_BUTTON_A: // Physical B button -> Back/Cancel
            std::cout << "Button A pressed (Physical B) - navigating up" << std::endl;
            navigateUp();
            break;

        // Menu button (physical button 10)
        case SDL_CONTROLLER_BUTTON_BACK:
            std::cout << "Back button pressed - exiting" << std::endl;
            m_running = false;
            break;
#else
        // Standard platforms: Physical A = SDL_A, Physical B = SDL_B
        case SDL_CONTROLLER_BUTTON_A:
            navigateInto();
            break;

        case SDL_CONTROLLER_BUTTON_B:
            navigateUp();
            break;
#endif

        case SDL_CONTROLLER_BUTTON_START:
        case SDL_CONTROLLER_BUTTON_GUIDE:
            m_running = false;
            break;

        default:
            break;
        }
        break;

    default:
        break;
    }
}

void FileBrowser::navigateUp()
{
#ifdef TG5040_PLATFORM
    // Don't allow navigation above /mnt/SDCARD on TG5040
    if (m_currentPath == "/mnt/SDCARD" || m_currentPath == "/")
    {
        std::cout << "navigateUp: Already at base directory: " << m_currentPath << std::endl;
        return;
    }
#else
    // Don't allow navigation above root on other platforms
    if (m_currentPath == "/")
    {
        std::cout << "navigateUp: Already at root" << std::endl;
        return;
    }
#endif

    // Get parent directory
    size_t lastSlash = m_currentPath.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash > 0)
    {
        std::string parentPath = m_currentPath.substr(0, lastSlash);
        if (parentPath.empty())
        {
            parentPath = "/";
        }

        std::cout << "navigateUp: Moving from '" << m_currentPath << "' to '" << parentPath << "'" << std::endl;

        if (scanDirectory(parentPath))
        {
            m_currentPath = parentPath;
            std::cout << "navigateUp: Successfully changed to: " << m_currentPath << std::endl;
        }
        else
        {
            std::cout << "Failed to scan parent directory: " << parentPath << std::endl;
        }
    }
}

void FileBrowser::navigateInto()
{
    if (m_entries.empty() || m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_entries.size()))
    {
        std::cout << "navigateInto: Invalid state - entries=" << m_entries.size()
                  << " selectedIndex=" << m_selectedIndex << std::endl;
        return;
    }

    const FileEntry& entry = m_entries[m_selectedIndex];

    std::cout << "navigateInto: Selected '" << entry.name << "' (dir=" << entry.isDirectory
              << ") path='" << entry.fullPath << "'" << std::endl;

    if (entry.isDirectory)
    {
        // Make a copy of the path before calling scanDirectory to avoid reference issues
        std::string targetPath = entry.fullPath;
        std::cout << "Attempting to scan directory: " << targetPath << std::endl;

        if (scanDirectory(targetPath))
        {
            m_currentPath = targetPath;
            std::cout << "Successfully changed to: " << m_currentPath << std::endl;
        }
        else
        {
            std::cout << "Failed to scan directory: " << targetPath << std::endl;
        }
    }
    else
    {
        // Select file and exit
        std::cout << "File selected: " << entry.fullPath << std::endl;
        m_selectedFile = entry.fullPath;
        m_running = false;
    }
}
