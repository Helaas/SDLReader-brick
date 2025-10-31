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
#include <cmath>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <sys/stat.h>
#include <vector>
#include <cfloat>

#include "mupdf_document.h"

namespace
{
std::string truncateToWidth(const std::string& text, float maxWidth)
{
    if (text.empty())
    {
        return text;
    }

    ImFont* font = ImGui::GetFont();
    if (!font)
    {
        return text;
    }

    const float fullWidth = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text.c_str()).x;
    if (fullWidth <= maxWidth)
    {
        return text;
    }

    static const std::string ellipsis = "...";
    const float ellipsisWidth = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, ellipsis.c_str()).x;

    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        result.push_back(text[i]);
        float width = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, result.c_str()).x;
        if (width + ellipsisWidth > maxWidth)
        {
            if (!result.empty())
            {
                result.pop_back();
            }
            break;
        }
    }

    if (result.empty())
    {
        return ellipsis;
    }

    result.append(ellipsis);
    return result;
}
} // namespace

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
      m_selectedIndex(0), m_gameController(nullptr), m_gameControllerInstanceID(-1),
      m_dpadUpHeld(false), m_dpadDownHeld(false), m_lastScrollTime(0)
{
}

FileBrowser::~FileBrowser()
{
    cleanup();
    clearThumbnailCache();
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
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
    }

    ImGuiIO& io = ImGui::GetIO();
    // Reset config flags for file browser
    io.ConfigFlags = 0;                                                                          // Clear all flags first
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad); // rely on bespoke navigation to avoid ImGui's multi-highlight state

#ifdef TG5040_PLATFORM
    // FileBrowser uses 3x scaling for large, readable text on 640x480 display
    // Always reset to default style first to avoid cumulative scaling
    ImGui::GetStyle() = ImGuiStyle();      // Reset to default
    ImGui::StyleColorsDark();              // Apply dark colors
    ImGui::GetStyle().ScaleAllSizes(3.0f); // Scale to 3x from base
    io.FontGlobalScale = 3.0f;
#else
    // Reset the style colors for non-TG5040 platforms
    ImGui::StyleColorsDark();
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
        } else {
        } });

    // Register pre-sleep callback - file browser has no UI windows to close
    m_powerHandler->setPreSleepCallback([this]() -> bool
                                        {
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
        m_initialized = false;
    }

    // Close game controller
    if (m_gameController)
    {
        SDL_GameControllerClose(m_gameController);
        m_gameController = nullptr;
        m_gameControllerInstanceID = -1;
    }

    clearThumbnailCache();
}

bool FileBrowser::scanDirectory(const std::string& path)
{
    // Make a defensive copy to avoid reference corruption issues
    const std::string safePath(path);
    std::cout << "scanDirectory: Scanning '" << safePath << "'" << std::endl;

    m_entries.clear();
    m_selectedIndex = 0;
    clearThumbnailCache();

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

void FileBrowser::clearThumbnailCache()
{
    m_thumbnailCache.clear();
}

SDL_Texture* FileBrowser::createTextureFromPixels(const std::vector<uint32_t>& pixels, int width, int height)
{
    if (width <= 0 || height <= 0 || pixels.empty())
    {
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface)
    {
        return nullptr;
    }

    std::memcpy(surface->pixels, pixels.data(), static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(uint32_t));

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

SDL_Texture* FileBrowser::createSolidTexture(int width, int height, SDL_Color color, Uint8 alpha)
{
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface)
    {
        return nullptr;
    }

    Uint32 fillColor = SDL_MapRGBA(surface->format, color.r, color.g, color.b, alpha);
    SDL_FillRect(surface, nullptr, fillColor);

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

FileBrowser::ThumbnailData& FileBrowser::getOrCreateThumbnail(const FileEntry& entry)
{
    auto& data = m_thumbnailCache[entry.fullPath];
    if (!data.texture && !data.failed)
    {
        if (!generateThumbnail(entry, data))
        {
            data.failed = true;
        }
    }
    return data;
}

bool FileBrowser::generateDirectoryThumbnail(const FileEntry& entry, ThumbnailData& data)
{
    (void) entry; // Directory thumbnails are generic

    const int width = THUMBNAIL_MAX_DIM;
    const int height = static_cast<int>(THUMBNAIL_MAX_DIM * 0.75f);
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

    const uint32_t folderBody = 0xFFE2A95Fu; // Warm folder color
    const uint32_t folderTab = 0xFFF4D3A8u;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            bool isTab = (y < height / 3) && (x < static_cast<int>(width * 0.7f)) && (y < height / 4);
            pixels[static_cast<size_t>(y) * width + x] = isTab ? folderTab : folderBody;
        }
    }

    SDL_Texture* texture = createTextureFromPixels(pixels, width, height);
    if (!texture)
    {
        return false;
    }

    data.texture.reset(texture);
    data.width = width;
    data.height = height;
    data.failed = false;
    return true;
}

bool FileBrowser::generateThumbnail(const FileEntry& entry, ThumbnailData& data)
{
    if (entry.isDirectory)
    {
        return generateDirectoryThumbnail(entry, data);
    }

    try
    {
        MuPdfDocument document;
        document.setMaxRenderSize(THUMBNAIL_MAX_DIM * 4, THUMBNAIL_MAX_DIM * 4);
        if (!document.open(entry.fullPath))
        {
            return false;
        }

        int pageCount = document.getPageCount();
        if (pageCount <= 0)
        {
            document.close();
            return false;
        }

        int nativeWidth = document.getPageWidthNative(0);
        int nativeHeight = document.getPageHeightNative(0);
        if (nativeWidth <= 0 || nativeHeight <= 0)
        {
            document.close();
            return false;
        }

        int maxDimension = std::max(nativeWidth, nativeHeight);
        float targetScale = static_cast<float>(THUMBNAIL_MAX_DIM) / static_cast<float>(std::max(1, maxDimension));
        int zoom = static_cast<int>(std::round(targetScale * 100.0f));
        zoom = std::clamp(zoom, 10, 200);

        int width = 0;
        int height = 0;
        auto bufferPtr = document.renderPageARGB(0, width, height, zoom);
        document.close();

        if (!bufferPtr || bufferPtr->empty() || width <= 0 || height <= 0)
        {
            return false;
        }

        std::vector<uint32_t> pixels(bufferPtr->begin(), bufferPtr->end());

        SDL_Texture* texture = createTextureFromPixels(pixels, width, height);
        if (!texture)
        {
            return false;
        }

        data.texture.reset(texture);
        data.width = width;
        data.height = height;
        data.failed = false;
        return true;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Thumbnail generation failed for \"" << entry.fullPath << "\": " << ex.what() << std::endl;
        return false;
    }
}

void FileBrowser::toggleViewMode()
{
    m_thumbnailView = !m_thumbnailView;
    m_gridColumns = 1;
    clampSelection();
}

void FileBrowser::moveSelectionVertical(int direction)
{
    if (m_entries.empty())
    {
        return;
    }

    if (m_thumbnailView && m_gridColumns > 0)
    {
        int newIndex = m_selectedIndex + direction * m_gridColumns;
        if (newIndex < 0)
        {
            newIndex = 0;
        }
        else if (newIndex >= static_cast<int>(m_entries.size()))
        {
            newIndex = static_cast<int>(m_entries.size()) - 1;
        }
        m_selectedIndex = newIndex;
    }
    else
    {
        const int total = static_cast<int>(m_entries.size());
        m_selectedIndex = (m_selectedIndex + direction + total) % total;
    }
}

void FileBrowser::moveSelectionHorizontal(int direction)
{
    if (m_entries.empty())
    {
        return;
    }

    if (m_thumbnailView && m_gridColumns > 0)
    {
        int newIndex = m_selectedIndex + direction;
        if (newIndex < 0)
        {
            newIndex = 0;
        }
        else if (newIndex >= static_cast<int>(m_entries.size()))
        {
            newIndex = static_cast<int>(m_entries.size()) - 1;
        }
        m_selectedIndex = newIndex;
    }
}

void FileBrowser::clampSelection()
{
    if (m_entries.empty())
    {
        m_selectedIndex = 0;
        return;
    }

    if (m_selectedIndex < 0)
    {
        m_selectedIndex = 0;
    }
    else if (m_selectedIndex >= static_cast<int>(m_entries.size()))
    {
        m_selectedIndex = static_cast<int>(m_entries.size()) - 1;
    }
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

        // Handle continuous scrolling when D-pad is held
        Uint32 currentTime = SDL_GetTicks();
        if ((m_dpadUpHeld || m_dpadDownHeld) && !m_entries.empty())
        {
            // Calculate delay based on whether we're in initial hold or repeat phase
            Uint32 delay = (m_lastScrollTime == 0) ? 0 : ((currentTime - m_lastScrollTime < SCROLL_INITIAL_DELAY_MS) ? SCROLL_INITIAL_DELAY_MS : SCROLL_REPEAT_DELAY_MS);

            if (m_lastScrollTime == 0 || (currentTime - m_lastScrollTime >= delay))
            {
                if (m_dpadUpHeld)
                {
                    moveSelectionVertical(-1);
                }
                else if (m_dpadDownHeld)
                {
                    moveSelectionVertical(1);
                }

                m_lastScrollTime = currentTime;
            }
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

    // Clear ImGui IO state before cleanup to prevent state bleeding
    if (m_initialized)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.WantCaptureKeyboard = false;
        io.WantCaptureMouse = false;
        io.NavActive = false;
        io.NavVisible = false;
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
    m_lastWindowWidth = windowWidth;
    m_lastWindowHeight = windowHeight;

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
    ImGui::TextWrapped("Cur. Directory: %s", m_currentPath.c_str());
    ImGui::Separator();

    // Instructions
#ifdef TG5040_PLATFORM
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "D-Pad: Nav. | A: Select | B: Back | X: Toggle View | Menu: Quit");
#else
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Arrow Keys/D-Pad: Navigate | Enter/A: Select | Backspace/B: Back | X: Toggle View | Escape: Quit");
#endif
    ImGui::Separator();

    if (m_thumbnailView)
    {
        renderThumbnailView(windowWidth, windowHeight);
    }
    else
    {
        renderListView(windowWidth, windowHeight);
        m_gridColumns = 1;
    }

    // Status bar
    ImGui::Separator();
    if (m_entries.empty())
    {
        ImGui::Text("No files or directories found");
    }
    else
    {
        int directoryCount = static_cast<int>(std::count_if(m_entries.begin(), m_entries.end(),
                                                            [](const FileEntry& e)
                                                            { return e.isDirectory; }));
        int fileCount = static_cast<int>(m_entries.size()) - directoryCount;
        ImGui::Text("%zu items (%d directories, %d files) | View: %s",
                    m_entries.size(), directoryCount, fileCount,
                    m_thumbnailView ? "Thumbnail" : "List");
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

void FileBrowser::renderListView(int windowWidth, int windowHeight)
{
    (void) windowWidth;
    (void) windowHeight;

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

            if (ImGui::IsMouseDoubleClicked(0))
            {
                navigateInto();
            }
        }

        if (isSelected)
        {
            ImGui::SetScrollHereY(0.5f);
        }
    }

    ImGui::EndChild();
}

void FileBrowser::renderThumbnailView(int windowWidth, int windowHeight)
{
    (void) windowHeight;

    ImGui::BeginChild("ThumbnailGrid", ImVec2(0, -40), true, ImGuiWindowFlags_HorizontalScrollbar);

    const float tilePadding = 20.0f;
    const float thumbRegionSize = static_cast<float>(THUMBNAIL_MAX_DIM);
    const float tileWidth = thumbRegionSize + tilePadding;

    float contentWidth = ImGui::GetContentRegionAvail().x;
    if (contentWidth <= 0.0f)
    {
        contentWidth = static_cast<float>(windowWidth) - 32.0f;
    }

    int columns = std::max(1, static_cast<int>(std::floor((contentWidth + tilePadding * 0.5f) / tileWidth)));
    m_gridColumns = columns;

    if (ImGui::BeginTable("ThumbnailTable", columns, ImGuiTableFlags_SizingFixedFit))
    {
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));

            const FileEntry& entry = m_entries[i];
            ThumbnailData& thumb = getOrCreateThumbnail(entry);
            SDL_Texture* texture = thumb.texture.get();
            bool isSelected = (static_cast<int>(i) == m_selectedIndex);

            const float tileHeight = thumbRegionSize + 48.0f;
            ImVec2 tileSize(tileWidth, tileHeight);
            ImVec2 tileMin = ImGui::GetCursorScreenPos();

            ImGui::InvisibleButton("ThumbnailButton", ImVec2(tileWidth, tileHeight));
            bool hovered = ImGui::IsItemHovered();

            if (ImGui::IsItemClicked())
            {
                m_selectedIndex = static_cast<int>(i);
            }

            if (hovered && ImGui::IsMouseDoubleClicked(0))
            {
                m_selectedIndex = static_cast<int>(i);
                navigateInto();
            }

            ImVec2 tileMax(tileMin.x + tileSize.x, tileMin.y + tileSize.y);
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            ImU32 backgroundColor = IM_COL32(40, 40, 40, 230);
            if (hovered)
            {
                backgroundColor = IM_COL32(60, 60, 60, 240);
            }
            if (isSelected)
            {
                backgroundColor = IM_COL32(30, 60, 95, 240);
            }

            drawList->AddRectFilled(tileMin, tileMax, backgroundColor, 8.0f);
            ImU32 borderColor = isSelected ? IM_COL32(30, 144, 255, 255)
                                           : IM_COL32(90, 90, 90, hovered ? 255 : 200);
            drawList->AddRect(tileMin, tileMax, borderColor, 8.0f, 0, 2.0f);

            ImVec2 imageRegionMin(tileMin.x + 10.0f, tileMin.y + 10.0f);
            ImVec2 imageRegionMax(tileMin.x + tileWidth - 10.0f, tileMin.y + thumbRegionSize - 6.0f);

            if (texture)
            {
                float availableWidth = imageRegionMax.x - imageRegionMin.x;
                float availableHeight = imageRegionMax.y - imageRegionMin.y;
                float scale = std::min(availableWidth / static_cast<float>(thumb.width),
                                       availableHeight / static_cast<float>(thumb.height));
                scale = std::max(scale, 0.01f);
                ImVec2 imageSize(static_cast<float>(thumb.width) * scale,
                                 static_cast<float>(thumb.height) * scale);
                ImVec2 imageMin(imageRegionMin.x + (availableWidth - imageSize.x) * 0.5f,
                                 imageRegionMin.y + (availableHeight - imageSize.y) * 0.5f);
                ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
                drawList->AddImage(reinterpret_cast<ImTextureID>(texture), imageMin, imageMax);
            }
            else
            {
                ImVec2 placeholderMin(imageRegionMin.x + 6.0f, imageRegionMin.y + 6.0f);
                ImVec2 placeholderMax(imageRegionMax.x - 6.0f, imageRegionMax.y - 6.0f);
                drawList->AddRect(placeholderMin, placeholderMax, IM_COL32(180, 180, 180, 180), 6.0f, 0, 2.0f);

                const char* message = thumb.failed ? "N/A" : "Loading";
                ImVec2 textSize = ImGui::CalcTextSize(message);
                ImVec2 messagePos(tileMin.x + (tileWidth - textSize.x) * 0.5f,
                                   tileMin.y + (thumbRegionSize - textSize.y) * 0.5f);
                drawList->AddText(messagePos, IM_COL32(230, 230, 230, 255), message);
            }

            float labelMaxWidth = tileWidth - 20.0f;
            std::string displayName = truncateToWidth(entry.name, labelMaxWidth);

            ImVec2 labelPos(tileMin.x + 10.0f, tileMin.y + thumbRegionSize + 6.0f);
            ImGui::SetCursorScreenPos(labelPos);
            ImGui::PushTextWrapPos(labelPos.x + labelMaxWidth);
            ImGui::TextUnformatted(displayName.c_str());
            ImGui::PopTextWrapPos();

            if (isSelected)
            {
                ImGui::SetScrollHereY(0.5f);
                ImGui::SetScrollHereX(0.5f);
            }

            ImGui::SetCursorScreenPos(tileMax);
            ImGui::Dummy(ImVec2(0.0f, 0.0f));

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
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
            moveSelectionVertical(-1);
            break;

        case SDLK_DOWN:
            moveSelectionVertical(1);
            break;

        case SDLK_LEFT:
            moveSelectionHorizontal(-1);
            break;

        case SDLK_RIGHT:
            moveSelectionHorizontal(1);
            break;

        case SDLK_RETURN:
        case SDLK_SPACE:
            navigateInto();
            break;

        case SDLK_BACKSPACE:
            navigateUp();
            break;

        case SDLK_x:
            toggleViewMode();
            break;

        default:
            break;
        }
        break;

    case SDL_CONTROLLERBUTTONDOWN:
        switch (event.cbutton.button)
        {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            if (!m_dpadUpHeld)
            {
                moveSelectionVertical(-1);
                m_dpadUpHeld = true;
                m_lastScrollTime = SDL_GetTicks();
            }
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (!m_dpadDownHeld)
            {
                moveSelectionVertical(1);
                m_dpadDownHeld = true;
                m_lastScrollTime = SDL_GetTicks();
            }
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            moveSelectionHorizontal(-1);
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            moveSelectionHorizontal(1);
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

        case SDL_CONTROLLER_BUTTON_X:
            toggleViewMode();
            break;
#else
        // Standard platforms: Physical A = SDL_A, Physical B = SDL_B
        case SDL_CONTROLLER_BUTTON_A:
            navigateInto();
            break;

        case SDL_CONTROLLER_BUTTON_B:
            navigateUp();
            break;

        case SDL_CONTROLLER_BUTTON_X:
            toggleViewMode();
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

    case SDL_CONTROLLERBUTTONUP:
        switch (event.cbutton.button)
        {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            m_dpadUpHeld = false;
            m_lastScrollTime = 0;
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            m_dpadDownHeld = false;
            m_lastScrollTime = 0;
            break;

        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
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
