#include "file_browser.h"
#include "path_utils.h"

#ifndef NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_FIXED_TYPES
#endif
#ifndef NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_IO
#endif
#ifndef NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_VARARGS
#endif
#ifndef NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#endif
#ifndef NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#endif
#ifndef NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_DEFAULT_FONT
#endif
#ifndef NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_FONT_BAKING
#endif
#include "nuklear.h"
#include "demo/sdl_renderer/nuklear_sdl_renderer.h"
#ifdef TG5040_PLATFORM
#include "power_handler.h"
#endif

#include <algorithm>
#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "mupdf_document.h"

#ifdef TG5040_PLATFORM
static constexpr SDL_GameControllerButton kAcceptButton = SDL_CONTROLLER_BUTTON_B;
static constexpr SDL_GameControllerButton kCancelButton = SDL_CONTROLLER_BUTTON_A;
#endif

namespace
{
std::filesystem::path normalizePath(const std::string& path)
{
    return std::filesystem::path(path).lexically_normal();
}

bool pathsEqual(const std::string& lhs, const std::string& rhs)
{
    return normalizePath(lhs) == normalizePath(rhs);
}

std::string truncateToWidth(const std::string& text, float maxWidth)
{
    if (text.empty())
    {
        return text;
    }

#ifdef TG5040_PLATFORM
    if (maxWidth <= 0.0f)
    {
        return text;
    }

    const float approxCharWidth = 14.0f; // heuristic for TG5040 font sizing
    size_t maxChars = static_cast<size_t>(std::floor(maxWidth / approxCharWidth));
    if (maxChars == 0)
    {
        return "...";
    }

    if (text.size() <= maxChars)
    {
        return text;
    }

    if (maxChars <= 3)
    {
        return "...";
    }

    std::string result = text.substr(0, maxChars - 3);
    result.append("...");
    return result;
#endif
}
} // namespace

FileBrowser::FileBrowser()
    : m_window(nullptr), m_renderer(nullptr), m_initialized(false), m_running(false),
      m_defaultRoot(getDefaultLibraryRoot()),
      m_lockToDefaultRoot([]
                          {
                              const char* root = std::getenv("SDL_READER_DEFAULT_DIR");
                              return root && root[0] != '\0'; }()),
      m_currentPath(m_defaultRoot),
      m_selectedIndex(0), m_selectedFile(), m_gameController(nullptr), m_gameControllerInstanceID(-1),
      m_thumbnailView(s_lastThumbnailView), m_gridColumns(1), m_lastWindowWidth(0), m_lastWindowHeight(0),
      m_dpadUpHeld(false), m_dpadDownHeld(false), m_lastScrollTime(0)
{
    if (s_cachedThumbnailsValid)
    {
        for (auto& [path, data] : s_cachedThumbnails)
        {
            data.pending = false;
        }
        m_thumbnailCache = std::move(s_cachedThumbnails);
        s_cachedThumbnailsValid = false;
    }
}

FileBrowser::~FileBrowser()
{
    cleanup(false);
    clearThumbnailCache();
    s_cachedThumbnails.clear();
    s_cachedThumbnailsValid = false;
    s_cachedDirectory.clear();
    s_lastThumbnailView = m_thumbnailView;
}

bool FileBrowser::initialize(SDL_Window* window, SDL_Renderer* renderer, const std::string& startPath)
{
    m_window = window;
    m_renderer = renderer;
    m_currentPath = startPath.empty() ? m_defaultRoot : startPath;

    m_ctx = nk_sdl_init(m_window, m_renderer);
    if (!m_ctx)
    {
        std::cerr << "Failed to initialize Nuklear SDL context" << std::endl;
        return false;
    }

    struct nk_font_atlas* atlas = nullptr;
    nk_sdl_font_stash_begin(&atlas);
    nk_sdl_font_stash_end();

    setupNuklearStyle();

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

        std::vector<std::string> candidates;
        auto addCandidate = [&candidates](const std::string& candidate)
        {
            if (candidate.empty())
                return;
            if (std::none_of(candidates.begin(), candidates.end(),
                             [&](const std::string& existing)
                             { return pathsEqual(existing, candidate); }))
            {
                candidates.push_back(candidate);
            }
        };

        addCandidate(m_defaultRoot);
        if (const char* home = std::getenv("HOME"))
        {
            addCandidate(home);
        }
        addCandidate("/");

        bool loaded = false;
        for (const auto& candidate : candidates)
        {
            if (scanDirectory(candidate))
            {
                m_currentPath = candidate;
                loaded = true;
                break;
            }
        }

        if (!loaded)
        {
            std::cerr << "Failed to scan fallback directories." << std::endl;
            return false;
        }
    }

    m_currentPath = normalizePath(m_currentPath).string();

    startThumbnailWorker();

    m_initialized = true;
    return true;
}

void FileBrowser::cleanup(bool preserveThumbnails)
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
        nk_sdl_shutdown();
        m_ctx = nullptr;
        m_initialized = false;
    }

    // Close game controller
    if (m_gameController)
    {
        SDL_GameControllerClose(m_gameController);
        m_gameController = nullptr;
        m_gameControllerInstanceID = -1;
    }

    stopThumbnailWorker();

    if (preserveThumbnails && m_thumbnailView)
    {
        clearPendingThumbnails();
        {
            std::lock_guard<std::mutex> lock(m_thumbnailMutex);
            s_cachedThumbnails = std::move(m_thumbnailCache);
            s_cachedThumbnailsValid = true;
            s_cachedDirectory = m_currentPath;
        }
    }
    else
    {
        clearThumbnailCache();
        s_cachedThumbnails.clear();
        s_cachedThumbnailsValid = false;
        s_cachedDirectory.clear();
    }

    s_lastThumbnailView = m_thumbnailView;
}

bool FileBrowser::scanDirectory(const std::string& path)
{
    // Make a defensive copy to avoid reference corruption issues
    const std::string safePath(path);
    std::cout << "scanDirectory: Scanning '" << safePath << "'" << std::endl;

    m_entries.clear();
    m_selectedIndex = 0;
    clearPendingThumbnails();
#ifdef TG5040_PLATFORM
    resetSelectionScrollTargets();
#endif

    bool reuseCachedThumbnails = s_cachedThumbnailsValid && pathsEqual(s_cachedDirectory, safePath);
    if (reuseCachedThumbnails)
    {
        m_thumbnailCache = std::move(s_cachedThumbnails);
        s_cachedThumbnailsValid = false;
        for (auto& [cachedPath, data] : m_thumbnailCache)
        {
            (void) cachedPath;
            data.pending = false;
        }
    }
    else
    {
        clearThumbnailCache();
        s_cachedThumbnails.clear();
        s_cachedThumbnailsValid = false;
        s_cachedDirectory.clear();
    }

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

        if (name == "..")
        {
            std::filesystem::path normalizedSafe = normalizePath(safePath);
            if (m_lockToDefaultRoot && pathsEqual(safePath, m_defaultRoot))
            {
                continue;
            }

            if (normalizedSafe == normalizedSafe.root_path())
            {
                continue;
            }
        }

        std::string fullPath;

        if (name == "..")
        {
            std::filesystem::path normalizedSafe = normalizePath(safePath);
            std::filesystem::path parent = normalizedSafe.parent_path();
            if (parent.empty())
            {
                parent = normalizedSafe.root_path();
            }
            if (parent.empty())
            {
                parent = "/";
            }
            fullPath = parent.string();
        }
        else
        {
            fullPath = safePath;
            if (!fullPath.empty() && fullPath.back() != '/')
            {
                fullPath += "/";
            }
            fullPath += name;
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
    std::lock_guard<std::mutex> lock(m_thumbnailMutex);
    m_thumbnailJobs.clear();
    m_thumbnailResults.clear();
}

SDL_Texture* FileBrowser::createTextureFromPixels(const std::vector<uint32_t>& pixels, int width, int height)
{
    if (width <= 0 || height <= 0 || pixels.empty())
    {
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture)
    {
        return nullptr;
    }

    void* texturePixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture, nullptr, &texturePixels, &pitch) != 0)
    {
        SDL_DestroyTexture(texture);
        return nullptr;
    }

    const size_t rowBytes = static_cast<size_t>(width) * sizeof(uint32_t);
    Uint8* dst = static_cast<Uint8*>(texturePixels);
    const Uint8* src = reinterpret_cast<const Uint8*>(pixels.data());
    for (int y = 0; y < height; ++y)
    {
        std::memcpy(dst + static_cast<size_t>(y) * pitch, src + static_cast<size_t>(y) * rowBytes, rowBytes);
    }

    SDL_UnlockTexture(texture);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
#endif
    return texture;
}

SDL_Texture* FileBrowser::createSolidTexture(int width, int height, SDL_Color color, Uint8 alpha)
{
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    SDL_PixelFormat* format = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
    if (!format)
    {
        return nullptr;
    }

    const Uint32 fillColor = SDL_MapRGBA(format, color.r, color.g, color.b, alpha);
    SDL_FreeFormat(format);

    std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height), fillColor);
    return createTextureFromPixels(pixels, width, height);
}

FileBrowser::ThumbnailData& FileBrowser::getOrCreateThumbnail(const FileEntry& entry)
{
    auto& data = m_thumbnailCache[entry.fullPath];

    if (entry.isDirectory)
    {
        if (!data.texture && !data.failed)
        {
            if (!generateDirectoryThumbnail(entry, data))
            {
                data.failed = true;
            }
        }
        return data;
    }

    if (!data.texture && !data.failed)
    {
        if (m_thumbnailThreadRunning)
        {
            if (!data.pending)
            {
                enqueueThumbnailJob(entry);
            }
        }
        else
        {
            if (!generateThumbnail(entry, data))
            {
                data.failed = true;
            }
        }
    }

    return data;
}

bool FileBrowser::buildDirectoryThumbnailPixels(std::vector<uint32_t>& pixels, int& width, int& height)
{
    width = THUMBNAIL_MAX_DIM;
    height = static_cast<int>(THUMBNAIL_MAX_DIM * 0.75f);
    pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

    const uint32_t folderBody = 0xFFE2A95Fu;
    const uint32_t folderTab = 0xFFF4D3A8u;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            bool isTab = (y < height / 3) && (x < static_cast<int>(width * 0.7f)) && (y < height / 4);
            pixels[static_cast<size_t>(y) * width + x] = isTab ? folderTab : folderBody;
        }
    }

    return true;
}

bool FileBrowser::buildDocumentThumbnailPixels(const FileEntry& entry, std::vector<uint32_t>& pixels, int& width, int& height)
{
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

        width = 0;
        height = 0;
        auto bufferPtr = document.renderPageARGB(0, width, height, zoom);
        document.close();

        if (!bufferPtr || bufferPtr->empty() || width <= 0 || height <= 0)
        {
            return false;
        }

        pixels.assign(bufferPtr->begin(), bufferPtr->end());
        return true;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Thumbnail generation failed for \"" << entry.fullPath << "\": " << ex.what() << std::endl;
        return false;
    }
}

bool FileBrowser::generateDirectoryThumbnail(const FileEntry& entry, ThumbnailData& data)
{
    (void) entry;
    std::vector<uint32_t> pixels;
    int width = 0;
    int height = 0;
    if (!buildDirectoryThumbnailPixels(pixels, width, height))
    {
        return false;
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
    data.pending = false;
    return true;
}

bool FileBrowser::generateThumbnail(const FileEntry& entry, ThumbnailData& data)
{
    std::vector<uint32_t> pixels;
    int width = 0;
    int height = 0;

    bool success = entry.isDirectory ? buildDirectoryThumbnailPixels(pixels, width, height)
                                     : buildDocumentThumbnailPixels(entry, pixels, width, height);
    if (!success)
    {
        return false;
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
    data.pending = false;
    return true;
}

void FileBrowser::startThumbnailWorker()
{
    if (m_thumbnailThreadRunning)
    {
        return;
    }

    m_thumbnailThreadStop = false;
    m_thumbnailThreadRunning = true;
    m_thumbnailThread = std::thread(&FileBrowser::thumbnailWorkerLoop, this);
}

void FileBrowser::stopThumbnailWorker()
{
    if (!m_thumbnailThreadRunning)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_thumbnailMutex);
        m_thumbnailThreadStop = true;
    }
    m_thumbnailCv.notify_all();

    if (m_thumbnailThread.joinable())
    {
        m_thumbnailThread.join();
    }

    m_thumbnailThreadRunning = false;
    m_thumbnailThreadStop = false;

    {
        std::lock_guard<std::mutex> lock(m_thumbnailMutex);
        m_thumbnailJobs.clear();
        m_thumbnailResults.clear();
    }

    for (auto& [path, cacheEntry] : m_thumbnailCache)
    {
        (void) path;
        cacheEntry.pending = false;
    }
}

void FileBrowser::requestThumbnailShutdown()
{
    if (!m_thumbnailThreadRunning)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_thumbnailMutex);
        m_thumbnailThreadStop = true;
        m_thumbnailJobs.clear();
        m_thumbnailResults.clear();
    }
    m_thumbnailCv.notify_all();
}

void FileBrowser::clearPendingThumbnails()
{
    std::lock_guard<std::mutex> lock(m_thumbnailMutex);
    m_thumbnailJobs.clear();
    m_thumbnailResults.clear();
    for (auto& [path, data] : m_thumbnailCache)
    {
        (void) path;
        data.pending = false;
    }
}

void FileBrowser::enqueueThumbnailJob(const FileEntry& entry)
{
    if (entry.isDirectory)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_thumbnailMutex);
    if (!m_thumbnailThreadRunning || m_thumbnailThreadStop)
    {
        return;
    }

    auto cacheIt = m_thumbnailCache.find(entry.fullPath);
    if (cacheIt != m_thumbnailCache.end())
    {
        if (cacheIt->second.texture || cacheIt->second.failed || cacheIt->second.pending)
        {
            return;
        }
    }

    for (const auto& job : m_thumbnailJobs)
    {
        if (job.fullPath == entry.fullPath)
        {
            if (cacheIt != m_thumbnailCache.end())
            {
                cacheIt->second.pending = true;
            }
            return;
        }
    }

    m_thumbnailJobs.emplace_back(entry.name, entry.fullPath, entry.isDirectory);
    if (cacheIt != m_thumbnailCache.end())
    {
        cacheIt->second.pending = true;
    }
    m_thumbnailCv.notify_one();
}

void FileBrowser::pumpThumbnailResults()
{
    std::deque<ThumbnailJobResult> ready;
    {
        std::lock_guard<std::mutex> lock(m_thumbnailMutex);
        if (m_thumbnailResults.empty())
        {
            return;
        }
        ready.swap(m_thumbnailResults);
    }

    for (auto& result : ready)
    {
        auto it = m_thumbnailCache.find(result.fullPath);
        if (it == m_thumbnailCache.end())
        {
            continue;
        }

        ThumbnailData& data = it->second;
        data.pending = false;

        if (!result.success)
        {
            data.failed = true;
            continue;
        }

        SDL_Texture* texture = createTextureFromPixels(result.pixels, result.width, result.height);
        if (!texture)
        {
            data.failed = true;
            continue;
        }

        data.texture.reset(texture);
        data.width = result.width;
        data.height = result.height;
        data.failed = false;
    }
}

void FileBrowser::thumbnailWorkerLoop()
{
    for (;;)
    {
        FileEntry job("", "", false);
        {
            std::unique_lock<std::mutex> lock(m_thumbnailMutex);
            m_thumbnailCv.wait(lock, [this]()
                               { return m_thumbnailThreadStop || !m_thumbnailJobs.empty(); });

            if (m_thumbnailThreadStop && m_thumbnailJobs.empty())
            {
                return;
            }

            job = m_thumbnailJobs.front();
            m_thumbnailJobs.pop_front();
        }

        ThumbnailJobResult result;
        result.fullPath = job.fullPath;

        if (!job.isDirectory)
        {
            std::vector<uint32_t> pixels;
            int width = 0;
            int height = 0;
            if (buildDocumentThumbnailPixels(job, pixels, width, height))
            {
                result.success = true;
                result.width = width;
                result.height = height;
                result.pixels = std::move(pixels);
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_thumbnailMutex);
            m_thumbnailResults.push_back(std::move(result));
        }
    }
}

void FileBrowser::toggleViewMode()
{
    m_thumbnailView = !m_thumbnailView;
    m_gridColumns = 1;
    clampSelection();
    s_lastThumbnailView = m_thumbnailView;
#ifdef TG5040_PLATFORM
    resetSelectionScrollTargets();
#endif
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

#ifdef TG5040_PLATFORM
    resetSelectionScrollTargets();
#endif
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

#ifdef TG5040_PLATFORM
    resetSelectionScrollTargets();
#endif
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
#ifdef TG5040_PLATFORM
        if (m_ctx)
        {
            nk_input_begin(m_ctx);
        }
#endif
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
        if (m_ctx)
        {
            nk_input_end(m_ctx);
        }
#endif

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

    // Cleanup Nuklear immediately so it doesn't interfere with main app
    bool preserveThumbnails = m_thumbnailView && !m_selectedFile.empty();
    cleanup(preserveThumbnails);

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
    if (!m_ctx)
    {
        return;
    }

    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);
    m_lastWindowWidth = windowWidth;
    m_lastWindowHeight = windowHeight;

    pumpThumbnailResults();

    SDL_SetRenderDrawColor(m_renderer, 30, 30, 30, 255);
    SDL_RenderClear(m_renderer);

    const float windowWidthF = static_cast<float>(windowWidth);
    const float windowHeightF = static_cast<float>(windowHeight);

    if (nk_begin(m_ctx, "SDLReader###File Browser", nk_rect(0.0f, 0.0f, windowWidthF, windowHeightF),
                 NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
    {
        const std::string pathLabel = "Cur. Directory: " + m_currentPath;
        nk_layout_row_dynamic(m_ctx, 26.0f, 1);
        nk_label(m_ctx, pathLabel.c_str(), NK_TEXT_LEFT);

        nk_layout_row_dynamic(m_ctx, 20.0f, 1);
        nk_label_colored(m_ctx, "D-Pad: Nav. | A: Select | B: Back | X: Toggle View | Menu: Quit",
                         NK_TEXT_LEFT, nk_rgb(180, 180, 180));

        nk_layout_row_dynamic(m_ctx, 6.0f, 1);
        nk_spacing(m_ctx, 1);

        const float contentHeight = std::max(60.0f, windowHeightF - 120.0f);

        if (m_thumbnailView)
        {
            renderThumbnailViewNuklear(contentHeight, windowWidth);
        }
        else
        {
            renderListViewNuklear(contentHeight, windowWidth);
            m_gridColumns = 1;
        }

        nk_layout_row_dynamic(m_ctx, 6.0f, 1);
        nk_spacing(m_ctx, 1);

        nk_layout_row_dynamic(m_ctx, 22.0f, 1);
        if (m_entries.empty())
        {
            nk_label(m_ctx, "No files or directories found", NK_TEXT_LEFT);
        }
        else
        {
            int directoryCount = static_cast<int>(std::count_if(m_entries.begin(), m_entries.end(),
                                                                [](const FileEntry& e)
                                                                { return e.isDirectory; }));
            int fileCount = static_cast<int>(m_entries.size()) - directoryCount;

            char statusBuffer[128];
            std::snprintf(statusBuffer, sizeof(statusBuffer), "%zu items (%d directories, %d files) | View: %s",
                          m_entries.size(), directoryCount, fileCount,
                          m_thumbnailView ? "Thumbnail" : "List");
            nk_label(m_ctx, statusBuffer, NK_TEXT_LEFT);
        }
    }
    nk_end(m_ctx);

    nk_sdl_render(NK_ANTI_ALIASING_ON);
    nk_sdl_handle_grab();
    SDL_RenderPresent(m_renderer);
}

void FileBrowser::renderListView(int windowWidth, int windowHeight)
{
    (void) windowWidth;
    (void) windowHeight;
}

void FileBrowser::renderThumbnailView(int windowWidth, int windowHeight)
{
    (void) windowWidth;
    (void) windowHeight;
}

void FileBrowser::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_QUIT)
    {
        m_running = false;
    }

    if (m_ctx
#ifdef TG5040_PLATFORM
        && !m_inFakeSleep
#endif
    )
    {
        nk_sdl_handle_event(const_cast<SDL_Event*>(&event));
    }

#ifdef TG5040_PLATFORM
    if (m_inFakeSleep)
    {
        return;
    }
#endif

    switch (event.type)
    {
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

    case SDL_KEYUP:
        switch (event.key.keysym.sym)
        {
        case SDLK_UP:
            m_dpadUpHeld = false;
            m_lastScrollTime = 0;
            break;
        case SDLK_DOWN:
            m_dpadDownHeld = false;
            m_lastScrollTime = 0;
            break;
        default:
            break;
        }
        break;

    case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.clicks >= 2)
        {
            navigateInto();
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
        case kAcceptButton:
            navigateInto();
            break;
        case kCancelButton:
            navigateUp();
            break;
#else
        case SDL_CONTROLLER_BUTTON_A:
            navigateInto();
            break;
        case SDL_CONTROLLER_BUTTON_B:
            navigateUp();
            break;
#endif
        case SDL_CONTROLLER_BUTTON_X:
            toggleViewMode();
            break;
        case SDL_CONTROLLER_BUTTON_BACK:
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
    const std::filesystem::path current = normalizePath(m_currentPath);
    std::filesystem::path rootPath = current.root_path();
    if (rootPath.empty())
    {
        rootPath = "/";
    }

    if (m_lockToDefaultRoot && pathsEqual(m_currentPath, m_defaultRoot))
    {
        std::cout << "navigateUp: Already at base directory: " << m_currentPath << std::endl;
        return;
    }

    if (current == rootPath)
    {
        std::cout << "navigateUp: Already at root" << std::endl;
        return;
    }

    std::filesystem::path parent = current.parent_path();
    if (parent.empty())
    {
        parent = rootPath;
    }

    std::string parentPath = parent.string();
    if (parentPath.empty())
    {
        parentPath = "/";
    }

    std::cout << "navigateUp: Moving from '" << m_currentPath << "' to '" << parentPath << "'" << std::endl;

    if (scanDirectory(parentPath))
    {
        m_currentPath = normalizePath(parentPath).string();
        std::cout << "navigateUp: Successfully changed to: " << m_currentPath << std::endl;
    }
    else
    {
        std::cout << "Failed to scan parent directory: " << parentPath << std::endl;
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
            m_currentPath = normalizePath(targetPath).string();
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
        if (m_thumbnailView)
        {
            requestThumbnailShutdown();
            clearPendingThumbnails();
        }
        m_selectedFile = entry.fullPath;
        m_running = false;
    }
}

bool FileBrowser::s_lastThumbnailView = false;
bool FileBrowser::s_cachedThumbnailsValid = false;
std::string FileBrowser::s_cachedDirectory;
std::unordered_map<std::string, FileBrowser::ThumbnailData> FileBrowser::s_cachedThumbnails;

void FileBrowser::setupNuklearStyle()
{
    if (!m_ctx)
    {
        return;
    }

    nk_style* style = &m_ctx->style;
    style->window.padding = nk_vec2(12.0f, 12.0f);
    style->window.spacing = nk_vec2(8.0f, 8.0f);
    style->window.border_color = nk_rgb(70, 70, 80);
    style->window.border = 1.0f;
    style->window.background = nk_rgba(26, 26, 30, 240);

    style->text.color = nk_rgb(235, 235, 235);

    style->button.normal = nk_style_item_color(nk_rgb(52, 58, 68));
    style->button.hover = nk_style_item_color(nk_rgb(70, 78, 92));
    style->button.active = nk_style_item_color(nk_rgb(83, 93, 110));
    style->button.border_color = nk_rgb(30, 30, 35);
    style->button.text_normal = nk_rgb(235, 235, 235);
    style->button.text_hover = nk_rgb(255, 255, 255);
    style->button.text_active = nk_rgb(255, 255, 255);

    style->scrollv.normal = nk_style_item_color(nk_rgba(60, 60, 70, 200));
    style->scrollv.hover = nk_style_item_color(nk_rgba(70, 70, 80, 220));
    style->scrollv.active = nk_style_item_color(nk_rgba(80, 80, 90, 255));
    style->scrollv.cursor_normal = nk_style_item_color(nk_rgba(100, 100, 110, 220));
    style->scrollv.cursor_hover = nk_style_item_color(nk_rgba(110, 110, 120, 230));
    style->scrollv.cursor_active = nk_style_item_color(nk_rgba(120, 120, 130, 255));

    style->scrollh = style->scrollv;
}

void FileBrowser::ensureSelectionVisible(float itemHeight, float viewHeight, float& scrollY, int& lastEnsureIndex)
{
    if (m_selectedIndex < 0 || m_entries.empty())
    {
        return;
    }

    float totalHeight = itemHeight * static_cast<float>(m_entries.size());
    if (totalHeight <= viewHeight)
    {
        scrollY = 0.0f;
        lastEnsureIndex = m_selectedIndex;
        return;
    }

    float targetTop = itemHeight * static_cast<float>(m_selectedIndex);
    float targetBottom = targetTop + itemHeight;

    if (targetTop < scrollY)
    {
        scrollY = targetTop;
    }
    else if (targetBottom > scrollY + viewHeight)
    {
        scrollY = targetBottom - viewHeight;
    }

    scrollY = std::clamp(scrollY, 0.0f, std::max(0.0f, totalHeight - viewHeight));
    lastEnsureIndex = m_selectedIndex;
}

void FileBrowser::renderListViewNuklear(float viewHeight, int windowWidth)
{
    (void) windowWidth;

    if (!m_ctx)
    {
        return;
    }

    const float itemHeight = 40.0f;
    const float clampedViewHeight = std::max(40.0f, viewHeight);

    if (m_lastListEnsureIndex != m_selectedIndex)
    {
        ensureSelectionVisible(itemHeight, clampedViewHeight, m_listScrollY, m_lastListEnsureIndex);
    }

    nk_layout_row_dynamic(m_ctx, clampedViewHeight, 1);
    nk_group_set_scroll(m_ctx, "FileList", 0, static_cast<nk_uint>(m_listScrollY));
    if (nk_group_begin(m_ctx, "FileList", NK_WINDOW_BORDER | NK_WINDOW_SCROLL_AUTO_HIDE))
    {
        nk_layout_row_dynamic(m_ctx, itemHeight, 1);
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            bool isSelected = (static_cast<int>(i) == m_selectedIndex);
            const FileEntry& entry = m_entries[i];
            std::string displayName = entry.isDirectory ? "[DIR] " + entry.name : entry.name;

            struct nk_style_button backup = m_ctx->style.button;
            if (isSelected)
            {
                m_ctx->style.button.normal = nk_style_item_color(nk_rgb(70, 110, 200));
                m_ctx->style.button.hover = nk_style_item_color(nk_rgb(90, 130, 220));
                m_ctx->style.button.active = nk_style_item_color(nk_rgb(100, 140, 230));
                m_ctx->style.button.text_normal = nk_rgb(255, 255, 255);
                m_ctx->style.button.text_hover = nk_rgb(255, 255, 255);
                m_ctx->style.button.text_active = nk_rgb(255, 255, 255);
            }

            if (nk_button_label(m_ctx, displayName.c_str()))
            {
                m_selectedIndex = static_cast<int>(i);
                m_selectedFile.clear();
                ensureSelectionVisible(itemHeight, clampedViewHeight, m_listScrollY, m_lastListEnsureIndex);
            }

            if (isSelected)
            {
                m_ctx->style.button = backup;
            }
        }

        nk_uint scrollX = 0;
        nk_uint scrollY = 0;
        nk_group_get_scroll(m_ctx, "FileList", &scrollX, &scrollY);
        m_listScrollY = static_cast<float>(scrollY);
        nk_group_end(m_ctx);
    }
}

void FileBrowser::renderThumbnailViewNuklear(float viewHeight, int windowWidth)
{
    if (!m_ctx)
    {
        return;
    }

    const float baseThumb = static_cast<float>(THUMBNAIL_MAX_DIM);
    const float tileWidth = baseThumb + 24.0f;
    const float tileHeight = baseThumb + 48.0f;
    int columns = std::max(1, windowWidth / static_cast<int>(tileWidth + 12.0f));
    const float clampedViewHeight = std::max(60.0f, viewHeight);

    if (m_lastThumbEnsureIndex != m_selectedIndex)
    {
        ensureSelectionVisible(tileHeight, clampedViewHeight, m_thumbnailScrollY, m_lastThumbEnsureIndex);
    }

    nk_layout_row_dynamic(m_ctx, clampedViewHeight, 1);
    nk_group_set_scroll(m_ctx, "ThumbnailGrid", 0, static_cast<nk_uint>(m_thumbnailScrollY));
    if (nk_group_begin(m_ctx, "ThumbnailGrid", NK_WINDOW_BORDER | NK_WINDOW_SCROLL_AUTO_HIDE))
    {
        if (!m_entries.empty())
        {
            nk_layout_row_begin(m_ctx, NK_STATIC, tileHeight, columns);
            for (size_t i = 0; i < m_entries.size(); ++i)
            {
                nk_layout_row_push(m_ctx, tileWidth);
                const FileEntry& entry = m_entries[i];
                ThumbnailData& thumb = getOrCreateThumbnail(entry);
                bool isSelected = (static_cast<int>(i) == m_selectedIndex);

                std::string displayName = entry.name;
#ifdef TG5040_PLATFORM
                displayName = truncateToWidth(entry.name, tileWidth - 16.0f);
#endif
                if (entry.isDirectory)
                {
                    displayName = "[DIR] " + displayName;
                }

                struct nk_style_button backup = m_ctx->style.button;
                if (isSelected)
                {
                    m_ctx->style.button.normal = nk_style_item_color(nk_rgb(70, 110, 200));
                    m_ctx->style.button.hover = nk_style_item_color(nk_rgb(90, 130, 220));
                    m_ctx->style.button.active = nk_style_item_color(nk_rgb(100, 140, 230));
                    m_ctx->style.button.text_normal = nk_rgb(255, 255, 255);
                    m_ctx->style.button.text_hover = nk_rgb(255, 255, 255);
                    m_ctx->style.button.text_active = nk_rgb(255, 255, 255);
                }

                nk_bool pressed = nk_false;
                if (thumb.texture)
                {
                    struct nk_image img = nk_image_ptr(static_cast<void*>(thumb.texture.get()));
                    pressed = nk_button_image_label(m_ctx, img, displayName.c_str(), NK_TEXT_CENTERED);
                }
                else
                {
                    pressed = nk_button_label(m_ctx, displayName.c_str());
                }

                if (pressed)
                {
                    m_selectedIndex = static_cast<int>(i);
                    m_selectedFile.clear();
                    ensureSelectionVisible(tileHeight, clampedViewHeight, m_thumbnailScrollY, m_lastThumbEnsureIndex);
                }

                if (isSelected)
                {
                    m_ctx->style.button = backup;
                }
            }
            nk_layout_row_end(m_ctx);
        }

        nk_uint scrollX = 0;
        nk_uint scrollY = 0;
        nk_group_get_scroll(m_ctx, "ThumbnailGrid", &scrollX, &scrollY);
        m_thumbnailScrollY = static_cast<float>(scrollY);
        nk_group_end(m_ctx);
    }
}

void FileBrowser::resetSelectionScrollTargets()
{
    m_lastListEnsureIndex = -1;
    m_lastThumbEnsureIndex = -1;
}

