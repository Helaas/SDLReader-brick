#include "file_browser.h"
#include <imgui.h>

#ifdef TG5040_PLATFORM
#include <imgui_impl_sdl.h>         // TG5040 uses v1.85 headers
#include <imgui_impl_sdlrenderer.h> // Compatible with SDL 2.0.9
#else
#include <imgui_impl_sdl2.h> // Modern platforms use v1.89+ headers
#include <imgui_impl_sdlrenderer2.h>
#endif

#include <algorithm>
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
      m_selectedIndex(0)
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

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

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
    if (m_initialized)
    {
#ifdef TG5040_PLATFORM
        ImGui_ImplSDLRenderer_Shutdown();
        ImGui_ImplSDL2_Shutdown();
#else
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
#endif
        ImGui::DestroyContext();
        m_initialized = false;
    }
}

bool FileBrowser::scanDirectory(const std::string& path)
{
    m_entries.clear();
    m_selectedIndex = 0;

    DIR* dir = opendir(path.c_str());
    if (!dir)
    {
        std::cerr << "Failed to open directory: " << path << std::endl;
        return false;
    }

    std::vector<FileEntry> directories;
    std::vector<FileEntry> files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;

        // Skip hidden files and current directory
        if (name.empty() || name[0] == '.')
        {
            // But allow ".." for parent directory navigation
            if (name != ".." || path == "/mnt/SDCARD" || path == "/")
            {
                continue;
            }
        }

        std::string fullPath = path;
        if (fullPath.back() != '/')
        {
            fullPath += "/";
        }
        fullPath += name;

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

    while (m_running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            handleEvent(event);
        }

        render();

        // Small delay to prevent CPU spinning
        SDL_Delay(10);
    }

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
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight)));

    ImGui::Begin("File Browser", nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse);

    // Display current path
    ImGui::TextWrapped("Current Directory: %s", m_currentPath.c_str());
    ImGui::Separator();

    // Instructions
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Use Arrow Keys or Mouse, Enter to Select, Escape to Quit");
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
#ifdef TG5040_PLATFORM
    ImGui_ImplSDL2_ProcessEvent(&event);
#else
    ImGui_ImplSDL2_ProcessEvent(&event);
#endif

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
            if (m_selectedIndex > 0)
            {
                m_selectedIndex--;
            }
            break;

        case SDLK_DOWN:
            if (m_selectedIndex < static_cast<int>(m_entries.size()) - 1)
            {
                m_selectedIndex++;
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
            if (m_selectedIndex > 0)
            {
                m_selectedIndex--;
            }
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (m_selectedIndex < static_cast<int>(m_entries.size()) - 1)
            {
                m_selectedIndex++;
            }
            break;

        case SDL_CONTROLLER_BUTTON_A:
            navigateInto();
            break;

        case SDL_CONTROLLER_BUTTON_B:
            navigateUp();
            break;

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
        return;
    }
#else
    // Don't allow navigation above root on other platforms
    if (m_currentPath == "/")
    {
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

        if (scanDirectory(parentPath))
        {
            m_currentPath = parentPath;
        }
    }
}

void FileBrowser::navigateInto()
{
    if (m_entries.empty() || m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_entries.size()))
    {
        return;
    }

    const FileEntry& entry = m_entries[m_selectedIndex];

    if (entry.isDirectory)
    {
        // Navigate into directory
        if (scanDirectory(entry.fullPath))
        {
            m_currentPath = entry.fullPath;
        }
    }
    else
    {
        // Select file and exit
        m_selectedFile = entry.fullPath;
        m_running = false;
    }
}
