#include "file_browser.h"
#include "options_manager.h"
#include "path_utils.h"
#include "mupdf_locking.h"

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
// IMPORTANT: nuklear.h MUST be included before nuklear_sdl_renderer.h
// The SDL renderer header depends on types defined in the main Nuklear header
#include "nuklear.h"
#include "demo/sdl_renderer/nuklear_sdl_renderer.h"
#ifdef TG5040_PLATFORM
#include "power_handler.h"
#include "power_events.h"
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include <mupdf/fitz.h>

#ifdef TG5040_PLATFORM
static constexpr SDL_GameControllerButton kAcceptButton = SDL_CONTROLLER_BUTTON_B;
static constexpr SDL_GameControllerButton kCancelButton = SDL_CONTROLLER_BUTTON_A;
#endif

#ifdef TG5040_PLATFORM
static constexpr SDL_GameControllerButton kToggleViewButton = SDL_CONTROLLER_BUTTON_Y;
#else
static constexpr SDL_GameControllerButton kToggleViewButton = SDL_CONTROLLER_BUTTON_X;
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

std::string truncateToWidth(const std::string& text, float maxWidth, const nk_user_font* font = nullptr)
{
    if (text.empty())
    {
        return text;
    }

    if (maxWidth <= 0.0f)
    {
        return text;
    }

    if (font)
    {
        const char* ellipsis = "...";
        const float ellipsisWidth = font->width(font->userdata, font->height, ellipsis, 3);
        if (ellipsisWidth >= maxWidth)
        {
            return "...";
        }

        if (font->width(font->userdata, font->height, text.c_str(), static_cast<int>(text.size())) <= maxWidth)
        {
            return text;
        }

        std::string result;
        result.reserve(text.size());
        for (char ch : text)
        {
            result.push_back(ch);
            float predictedWidth = font->width(font->userdata, font->height, result.c_str(),
                                               static_cast<int>(result.size()));
            if (predictedWidth + ellipsisWidth > maxWidth)
            {
                result.pop_back();
                break;
            }
        }
        if (result.empty())
        {
            return "...";
        }
        result.append("...");
        return result;
    }

    const float approxCharWidth = 10.0f;
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

    return text;
}
} // namespace

namespace
{
constexpr int kThumbnailWorkerCount = 2;

class QuickThumbnailRenderer
{
public:
    QuickThumbnailRenderer()
    {
        m_ctx = fz_new_context(nullptr, getSharedMuPdfLocks(), 64 << 20);
        if (!m_ctx)
        {
            throw std::runtime_error("Failed to create MuPDF context for thumbnails");
        }
        fz_register_document_handlers(m_ctx);
#ifdef TG5040_PLATFORM
        fz_set_aa_level(m_ctx, 2);
        fz_set_text_aa_level(m_ctx, 2);
        fz_set_graphics_aa_level(m_ctx, 1);
#else
        fz_set_aa_level(m_ctx, 4);
        fz_set_text_aa_level(m_ctx, 4);
        fz_set_graphics_aa_level(m_ctx, 2);
#endif
    }

    ~QuickThumbnailRenderer()
    {
        if (m_ctx)
        {
            fz_drop_context(m_ctx);
        }
    }

    bool renderFirstPage(const std::string& path, int maxDim, std::vector<uint32_t>& pixels, int& width, int& height)
    {
        fz_document* doc = nullptr;
        fz_page* page = nullptr;
        fz_device* dev = nullptr;
        fz_pixmap* pix = nullptr;
        bool success = true;
        fz_var(doc);
        fz_var(page);
        fz_var(dev);
        fz_var(pix);

        fz_try(m_ctx)
        {
            doc = fz_open_document(m_ctx, path.c_str());
            page = fz_load_page(m_ctx, doc, 0);

            fz_rect bounds = fz_bound_page(m_ctx, page);
            float pageWidth = bounds.x1 - bounds.x0;
            float pageHeight = bounds.y1 - bounds.y0;
            float maxDimension = std::max(pageWidth, pageHeight);
            float scale = (maxDimension > 0.0f) ? static_cast<float>(maxDim) / maxDimension : 1.0f;
#ifdef TG5040_PLATFORM
            scale = std::clamp(scale, 0.04f, 1.0f);
#else
            scale = std::clamp(scale, 0.05f, 1.5f);
#endif

            fz_matrix transform = fz_scale(scale, scale);
            fz_rect transformed = bounds;
            transformed = fz_transform_rect(transformed, transform);
            fz_irect bbox = fz_round_rect(transformed);

            pix = fz_new_pixmap_with_bbox(m_ctx, fz_device_rgb(m_ctx), bbox, nullptr, 1);
            fz_clear_pixmap_with_value(m_ctx, pix, 0xFF);

            dev = fz_new_draw_device(m_ctx, fz_identity, pix);
            fz_run_page(m_ctx, page, dev, transform, nullptr);
            fz_close_device(m_ctx, dev);
            fz_drop_device(m_ctx, dev);
            dev = nullptr;

            width = std::max(1, bbox.x1 - bbox.x0);
            height = std::max(1, bbox.y1 - bbox.y0);

            pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
            const unsigned char* samples = fz_pixmap_samples(m_ctx, pix);
            int stride = fz_pixmap_stride(m_ctx, pix);

            for (int y = 0; y < height; ++y)
            {
                const unsigned char* row = samples + static_cast<size_t>(y) * stride;
                for (int x = 0; x < width; ++x)
                {
                    const unsigned char* src = row + static_cast<size_t>(x) * 4;
                    uint32_t r = src[0];
                    uint32_t g = src[1];
                    uint32_t b = src[2];
                    uint32_t a = src[3];
                    pixels[static_cast<size_t>(y) * width + x] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
        fz_always(m_ctx)
        {
            if (dev)
            {
                fz_drop_device(m_ctx, dev);
            }
            if (pix)
            {
                fz_drop_pixmap(m_ctx, pix);
            }
            if (page)
            {
                fz_drop_page(m_ctx, page);
            }
            if (doc)
            {
                fz_drop_document(m_ctx, doc);
            }
        }
        fz_catch(m_ctx)
        {
            success = false;
        }

        return success;
    }

private:
    fz_context* m_ctx{nullptr};
};

bool renderQuickThumbnail(const std::string& path, int maxDim, std::vector<uint32_t>& pixels, int& width, int& height)
{
    try
    {
        thread_local QuickThumbnailRenderer renderer;
        return renderer.renderFirstPage(path, maxDim, pixels, width, height);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Thumbnail renderer init failed for \"" << path << "\": " << ex.what() << std::endl;
        return false;
    }
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
      m_dpadUpHeld(false), m_dpadDownHeld(false), m_lastScrollTime(0), m_waitingForInitialRepeat(false),
      m_leftHeld(false), m_rightHeld(false), m_lastHorizontalScrollTime(0), m_waitingForInitialHorizontalRepeat(false)
{
}

FileBrowser::~FileBrowser()
{
    cleanup(false);
    clearThumbnailCache();
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

    // Setup font atlas and load a reliable UI font
    struct nk_font_atlas* atlas = nullptr;
    nk_sdl_font_stash_begin(&atlas);
    struct nk_font* uiFont = nullptr;
    constexpr float kUiFontSize = 28.0f;

    if (atlas)
    {
        auto fontExists = [](const std::string& path) -> bool
        {
            if (path.empty())
            {
                return false;
            }
            std::error_code ec;
            return std::filesystem::exists(path, ec) && !ec;
        };

        // Load available fonts from options manager
        OptionsManager optionsManager;
        const auto& availableFonts = optionsManager.getAvailableFonts();
        for (const auto& fontInfo : availableFonts)
        {
            if (fontExists(fontInfo.filePath))
            {
                uiFont = nk_font_atlas_add_from_file(atlas, fontInfo.filePath.c_str(), kUiFontSize, nullptr);
                if (uiFont)
                {
                    atlas->default_font = uiFont;
                    std::cout << "FileBrowser: Loaded UI font from options list: " << fontInfo.displayName << std::endl;
                    break;
                }
            }
        }

        // Try fallback fonts if no font from options list worked
        if (!uiFont)
        {
            static const char* fallbackFonts[] = {
                "fonts/Inter-Regular.ttf",
                "fonts/Roboto-Regular.ttf",
                "fonts/JetBrainsMono-Regular.ttf"};

            for (const char* path : fallbackFonts)
            {
                if (fontExists(path))
                {
                    uiFont = nk_font_atlas_add_from_file(atlas, path, kUiFontSize, nullptr);
                    if (uiFont)
                    {
                        atlas->default_font = uiFont;
                        std::cout << "FileBrowser: Loaded fallback UI font: " << path << std::endl;
                        break;
                    }
                }
            }
        }

        // Use Nuklear default font as last resort
        if (!uiFont)
        {
            uiFont = nk_font_atlas_add_default(atlas, kUiFontSize, nullptr);
            if (uiFont)
            {
                atlas->default_font = uiFont;
                std::cout << "FileBrowser: Loaded Nuklear default font for UI fallback" << std::endl;
            }
            else
            {
                std::cerr << "FileBrowser: Failed to load any UI font; interface text may be unavailable" << std::endl;
            }
        }
    }

    nk_sdl_font_stash_end();
    if (uiFont)
    {
        nk_style_set_font(m_ctx, &uiFont->handle);
    }

    setupNuklearStyle();

#ifdef TG5040_PLATFORM
    // Initialize power handler
    m_powerHandler = std::make_unique<PowerHandler>();

    m_powerMessageEventType = getPowerMessageEventType();
    auto pushPowerMessageEvent = [this](const std::string& message)
    {
        SDL_Event event;
        SDL_zero(event);
        event.type = m_powerMessageEventType;
        event.user.code = 0;
        event.user.data1 = new std::string(message);
        event.user.data2 = nullptr;
        if (SDL_PushEvent(&event) < 0)
        {
            delete static_cast<std::string*>(event.user.data1);
            std::cerr << "FileBrowser: Failed to push power message event: " << SDL_GetError() << std::endl;
        }
    };

    // Display shutdown and power-related messages in the browser UI
    m_powerHandler->setErrorCallback([pushPowerMessageEvent](const std::string& message)
                                     { pushPowerMessageEvent(message); });

    // Register sleep mode callback for fake sleep functionality
    m_powerHandler->setSleepModeCallback([this](bool enterFakeSleep)
                                         { m_inFakeSleep = enterFakeSleep; });

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
    if (preserveThumbnails)
    {
        clearPendingThumbnails();
    }
    else
    {
        clearThumbnailCache();
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
    resetSelectionScrollTargets();

    DIR* dir = opendir(safePath.c_str());
    if (!dir)
    {
        std::cerr << "Failed to open directory: " << safePath << " - " << strerror(errno) << std::endl;
        return false;
    }

    std::optional<FileEntry> parentEntry;
    std::vector<FileEntry> directories;
    std::vector<FileEntry> files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;
        const bool isParentLink = (name == "..");

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

        if (isParentLink)
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

        if (isParentLink)
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
                if (isParentLink)
                {
                    parentEntry = FileEntry(name, fullPath, true, true);
                }
                else
                {
                    directories.emplace_back(name, fullPath, true, false);
                }
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
    if (parentEntry.has_value())
    {
        m_entries.push_back(*parentEntry);
    }
    m_entries.insert(m_entries.end(), directories.begin(), directories.end());
    m_entries.insert(m_entries.end(), files.begin(), files.end());

    std::cout << "scanDirectory: Found " << directories.size() << " directories and "
              << files.size() << " files (" << m_entries.size() << " total)" << std::endl;

    tryRestoreSelection(safePath);

    return true;
}

void FileBrowser::tryRestoreSelection(const std::string& directoryPath)
{
    if (!m_restoreSelectionPending || m_restoreSelectionPath.empty() || m_entries.empty())
    {
        return;
    }

    const std::string normalizedDir = normalizePath(directoryPath).string();
    std::filesystem::path targetPath = normalizePath(m_restoreSelectionPath);
    std::filesystem::path targetDir = targetPath.parent_path();
    if (targetDir.empty())
    {
        targetDir = targetPath.root_path();
    }

    if (targetDir.empty() || !pathsEqual(targetDir.string(), normalizedDir))
    {
        return;
    }

    const std::string resolvedTarget = targetPath.string();
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (pathsEqual(m_entries[i].fullPath, resolvedTarget))
        {
            m_selectedIndex = static_cast<int>(i);
            resetSelectionScrollTargets();
            break;
        }
    }

    m_restoreSelectionPending = false;
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
    m_thumbnailUsage.clear();
    m_thumbnailUsageLookup.clear();
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
        if (data.texture)
        {
            recordThumbnailUsage(entry.fullPath);
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

    if (data.texture)
    {
        recordThumbnailUsage(entry.fullPath);
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
    if (!renderQuickThumbnail(entry.fullPath, THUMBNAIL_MAX_DIM, pixels, width, height))
    {
        std::cerr << "Thumbnail generation failed for \"" << entry.fullPath << "\"" << std::endl;
        return false;
    }
    return true;
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
    m_thumbnailThreads.clear();
    m_thumbnailThreads.reserve(kThumbnailWorkerCount);
    for (int i = 0; i < kThumbnailWorkerCount; ++i)
    {
        m_thumbnailThreads.emplace_back(&FileBrowser::thumbnailWorkerLoop, this);
    }
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

    for (auto& worker : m_thumbnailThreads)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    m_thumbnailThreads.clear();

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
    clearPendingThumbnails();
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

void FileBrowser::cancelThumbnailJobsForPath(const std::string& path)
{
    std::lock_guard<std::mutex> lock(m_thumbnailMutex);

    for (auto jobIt = m_thumbnailJobs.begin(); jobIt != m_thumbnailJobs.end();)
    {
        if (jobIt->fullPath == path)
        {
            jobIt = m_thumbnailJobs.erase(jobIt);
        }
        else
        {
            ++jobIt;
        }
    }

    for (auto resultIt = m_thumbnailResults.begin(); resultIt != m_thumbnailResults.end();)
    {
        if (resultIt->fullPath == path)
        {
            resultIt = m_thumbnailResults.erase(resultIt);
        }
        else
        {
            ++resultIt;
        }
    }
}

void FileBrowser::removeThumbnailEntry(const std::string& path)
{
    auto cacheIt = m_thumbnailCache.find(path);
    if (cacheIt != m_thumbnailCache.end())
    {
        m_thumbnailCache.erase(cacheIt);
    }

    auto usageIt = m_thumbnailUsageLookup.find(path);
    if (usageIt != m_thumbnailUsageLookup.end())
    {
        m_thumbnailUsage.erase(usageIt->second);
        m_thumbnailUsageLookup.erase(usageIt);
    }

    cancelThumbnailJobsForPath(path);
}

void FileBrowser::recordThumbnailUsage(const std::string& path)
{
    auto usageIt = m_thumbnailUsageLookup.find(path);
    if (usageIt != m_thumbnailUsageLookup.end())
    {
        m_thumbnailUsage.splice(m_thumbnailUsage.begin(), m_thumbnailUsage, usageIt->second);
    }
    else
    {
        m_thumbnailUsage.emplace_front(path);
        m_thumbnailUsageLookup[path] = m_thumbnailUsage.begin();
    }

    evictOldThumbnails();
}

void FileBrowser::evictOldThumbnails()
{
    while (m_thumbnailUsage.size() > MAX_CACHED_THUMBNAILS)
    {
        const std::string victimPath = m_thumbnailUsage.back();
        removeThumbnailEntry(victimPath);
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

    m_thumbnailJobs.emplace_back(entry.name, entry.fullPath, entry.isDirectory, entry.isParentLink);
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
        recordThumbnailUsage(result.fullPath);
    }
}

void FileBrowser::thumbnailWorkerLoop()
{
    for (;;)
    {
        FileEntry job("", "", false, false);
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
    resetSelectionScrollTargets();
}

void FileBrowser::moveSelectionVertical(int direction)
{
    if (m_entries.empty())
    {
        return;
    }

    if (m_thumbnailView && m_gridColumns > 0)
    {
        const int totalEntries = static_cast<int>(m_entries.size());
        const int columns = std::max(1, m_gridColumns);
        if (totalEntries == 0)
        {
            m_selectedIndex = 0;
        }
        else
        {
            const int totalRows = (totalEntries + columns - 1) / columns;
            const int clampedIndex = std::clamp(m_selectedIndex, 0, totalEntries - 1);
            const int currentRow = std::clamp(clampedIndex / columns, 0, std::max(0, totalRows - 1));
            const int currentCol = clampedIndex % columns;

            int nextRow = currentRow + direction;
            if (nextRow < 0)
            {
                nextRow = totalRows - 1;
            }
            else if (nextRow >= totalRows)
            {
                nextRow = 0;
            }

            int rowStart = nextRow * columns;
            int rowEnd = std::min(rowStart + columns - 1, totalEntries - 1);
            int nextIndex = rowStart + currentCol;
            if (nextIndex > rowEnd)
            {
                nextIndex = rowEnd;
            }
            m_selectedIndex = std::clamp(nextIndex, 0, totalEntries - 1);
        }
    }
    else if (!m_entries.empty())
    {
        const int total = static_cast<int>(m_entries.size());
        m_selectedIndex = (m_selectedIndex + direction + total) % total;
    }

    // Reset scroll targets to trigger auto-scroll on next render
    resetSelectionScrollTargets();
}

void FileBrowser::moveSelectionHorizontal(int direction)
{
    if (m_entries.empty())
    {
        return;
    }

    bool rowChanged = false;

    if (m_thumbnailView && m_gridColumns > 0)
    {
        const int totalEntries = static_cast<int>(m_entries.size());
        const int columns = std::max(1, m_gridColumns);
        if (totalEntries == 0)
        {
            return;
        }

        const int totalRows = (totalEntries + columns - 1) / columns;
        const int clampedIndex = std::clamp(m_selectedIndex, 0, totalEntries - 1);
        const int row = clampedIndex / columns;
        const int rowStart = row * columns;
        const int rowEnd = std::min(rowStart + columns - 1, totalEntries - 1);
        int newIndex = clampedIndex;

        if (direction > 0)
        {
            if (clampedIndex >= rowEnd)
            {
                int nextRow = row + 1;
                if (nextRow >= totalRows)
                {
                    newIndex = 0;
                }
                else
                {
                    newIndex = nextRow * columns;
                }
            }
            else
            {
                newIndex = std::min(clampedIndex + 1, totalEntries - 1);
            }
        }
        else if (direction < 0)
        {
            if (clampedIndex <= rowStart)
            {
                int prevRow = row - 1;
                if (prevRow < 0)
                {
                    int lastRowStart = (totalRows - 1) * columns;
                    int lastRowEnd = std::min(lastRowStart + columns - 1, totalEntries - 1);
                    newIndex = lastRowEnd;
                }
                else
                {
                    int prevRowStart = prevRow * columns;
                    int prevRowEnd = std::min(prevRowStart + columns - 1, totalEntries - 1);
                    newIndex = prevRowEnd;
                }
            }
            else
            {
                newIndex = std::max(clampedIndex - 1, 0);
            }
        }

        int newRow = newIndex / columns;
        rowChanged = newRow != row;
        m_selectedIndex = newIndex;

        // Only reset scroll targets if we moved to a different row
        // This prevents unnecessary scroll updates when moving within the same row
        if (rowChanged)
        {
            resetSelectionScrollTargets();
        }
    }
    else
    {
        // In list view, always reset scroll targets
        resetSelectionScrollTargets();
    }
}

void FileBrowser::jumpSelectionByLetter(int direction)
{
    if (m_entries.empty() || direction == 0)
    {
        return;
    }

    const int total = static_cast<int>(m_entries.size());
    const int startIndex = std::clamp(m_selectedIndex, 0, total - 1);

    int firstNonParentIndex = -1;
    int firstDirIndex = -1;
    int lastDirIndex = -1;
    int firstFileIndex = -1;
    int lastFileIndex = -1;
    for (int i = 0; i < total; ++i)
    {
        if (!m_entries[i].isParentLink && firstNonParentIndex == -1)
        {
            firstNonParentIndex = i;
        }
        if (m_entries[i].isDirectory && !m_entries[i].isParentLink)
        {
            if (firstDirIndex == -1)
            {
                firstDirIndex = i;
            }
            lastDirIndex = i;
        }
        if (!m_entries[i].isDirectory && firstFileIndex == -1)
        {
            firstFileIndex = i;
        }
        if (!m_entries[i].isDirectory)
        {
            lastFileIndex = i;
        }
    }

    auto extractKey = [](const FileEntry& entry) -> char
    {
        for (char ch : entry.name)
        {
            unsigned char uch = static_cast<unsigned char>(ch);
            if (std::isalpha(uch))
            {
                return static_cast<char>(std::tolower(uch));
            }
            if (!std::isspace(uch))
            {
                return static_cast<char>(std::tolower(uch));
            }
        }
        return 0;
    };

    const char currentKey = extractKey(m_entries[startIndex]);

    // Hard boundary rules to respect directory grouping.
    if (direction > 0)
    {
        if (m_entries[startIndex].isParentLink && firstNonParentIndex != -1)
        {
            m_selectedIndex = firstNonParentIndex;
            resetSelectionScrollTargets();
            return;
        }
        if (m_entries[startIndex].isDirectory && !m_entries[startIndex].isParentLink)
        {
            int dirTarget = -1;
            if (startIndex < lastDirIndex)
            {
                for (int i = startIndex + 1; i <= lastDirIndex; ++i)
                {
                    if (m_entries[i].isDirectory && !m_entries[i].isParentLink)
                    {
                        char key = extractKey(m_entries[i]);
                        if (key == 0)
                        {
                            continue;
                        }
                        if (currentKey == 0 || key > currentKey)
                        {
                            dirTarget = i;
                            break;
                        }
                    }
                }
            }
            if (dirTarget != -1)
            {
                m_selectedIndex = dirTarget;
                resetSelectionScrollTargets();
                return;
            }
            if (firstFileIndex != -1)
            {
                m_selectedIndex = firstFileIndex;
                resetSelectionScrollTargets();
                return;
            }
        }
    }
    else // direction < 0
    {
        if (m_entries[startIndex].isParentLink && total > 1)
        {
            m_selectedIndex = total - 1;
            resetSelectionScrollTargets();
            return;
        }
        if (m_entries[startIndex].isDirectory && !m_entries[startIndex].isParentLink &&
            startIndex == firstDirIndex && firstDirIndex > 0 && m_entries[0].isParentLink)
        {
            m_selectedIndex = 0;
            resetSelectionScrollTargets();
            return;
        }
        if (!m_entries[startIndex].isDirectory && lastDirIndex != -1)
        {
            // Any file jumps back to the last folder
            m_selectedIndex = lastDirIndex;
            resetSelectionScrollTargets();
            return;
        }
        if (m_entries[startIndex].isDirectory && !m_entries[startIndex].isParentLink && startIndex > firstDirIndex)
        {
            int dirTarget = -1;
            for (int i = startIndex - 1; i >= firstDirIndex; --i)
            {
                if (m_entries[i].isDirectory && !m_entries[i].isParentLink)
                {
                    char key = extractKey(m_entries[i]);
                    if (key == 0)
                    {
                        continue;
                    }
                    if (currentKey == 0 || key < currentKey)
                    {
                        dirTarget = i;
                        break;
                    }
                }
            }
            if (dirTarget != -1)
            {
                m_selectedIndex = dirTarget;
                resetSelectionScrollTargets();
                return;
            }
        }
    }

    auto isCandidate = [&](const FileEntry& entry, char key) -> bool
    {
        if (entry.isParentLink || key == 0 || key == currentKey)
        {
            return false;
        }
        if (direction > 0)
        {
            return currentKey == 0 || key > currentKey;
        }
        return currentKey == 0 || key < currentKey;
    };

    int targetIndex = -1;

    if (direction > 0)
    {
        int searchStart = startIndex + 1;
        int wrapStart = firstNonParentIndex;
        if (!m_entries[startIndex].isDirectory && firstFileIndex != -1)
        {
            searchStart = std::max(searchStart, firstFileIndex);
            wrapStart = firstFileIndex;
        }

        for (int i = searchStart; i < total; ++i)
        {
            char key = extractKey(m_entries[i]);
            if (isCandidate(m_entries[i], key))
            {
                targetIndex = i;
                break;
            }
        }

        if (targetIndex == -1 && wrapStart != -1)
        {
            for (int i = wrapStart; i <= startIndex; ++i)
            {
                char key = extractKey(m_entries[i]);
                if (isCandidate(m_entries[i], key))
                {
                    targetIndex = i;
                    break;
                }
            }
        }
    }
    else // direction < 0
    {
        int searchStart = startIndex - 1;
        int searchEnd = 0;
        int wrapStart = total - 1;
        if (!m_entries[startIndex].isDirectory && firstFileIndex != -1)
        {
            searchEnd = firstFileIndex;
            wrapStart = (lastFileIndex != -1) ? lastFileIndex : firstFileIndex;
        }

        for (int i = searchStart; i >= searchEnd; --i)
        {
            char key = extractKey(m_entries[i]);
            if (isCandidate(m_entries[i], key))
            {
                targetIndex = i;
                break;
            }
        }

        if (targetIndex == -1)
        {
            for (int i = wrapStart; i > startIndex; --i)
            {
                char key = extractKey(m_entries[i]);
                if (isCandidate(m_entries[i], key))
                {
                    targetIndex = i;
                    break;
                }
            }
        }

        // If no earlier letter was found for a file, fall back to the last directory.
        if (targetIndex == -1 && !m_entries[startIndex].isDirectory && lastDirIndex != -1)
        {
            targetIndex = lastDirIndex;
        }
    }

    if (targetIndex >= 0 && targetIndex < total && targetIndex != m_selectedIndex)
    {
        m_selectedIndex = targetIndex;
        resetSelectionScrollTargets();
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
            if (m_powerMessageEventType != 0 && event.type == m_powerMessageEventType)
            {
                std::unique_ptr<std::string> message(static_cast<std::string*>(event.user.data1));
                if (message)
                {
                    showPowerMessage(*message);
                }
                continue;
            }
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

        // If a file was chosen or the browser was asked to quit, exit immediately to
        // preserve the selected path before Nuklear UI processing clears it.
        if (!m_running)
        {
            break;
        }

        // Handle continuous scrolling when D-pad is held
        Uint32 currentTime = SDL_GetTicks();
        if ((m_dpadUpHeld || m_dpadDownHeld) && !m_entries.empty())
        {
            const Uint32 elapsed = (m_lastScrollTime <= currentTime) ? (currentTime - m_lastScrollTime) : 0;
            Uint32 baseDelay = m_waitingForInitialRepeat ? SCROLL_INITIAL_DELAY_MS : SCROLL_REPEAT_DELAY_MS;
            if (m_thumbnailView)
            {
                baseDelay *= THUMBNAIL_SCROLL_DELAY_FACTOR;
            }
            const Uint32 targetDelay = baseDelay;

            if (m_lastScrollTime == 0 || elapsed >= targetDelay)
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
                m_waitingForInitialRepeat = false;
            }
        }
        if ((m_leftHeld || m_rightHeld) && !m_entries.empty())
        {
            const Uint32 elapsed = (m_lastHorizontalScrollTime <= currentTime)
                                       ? (currentTime - m_lastHorizontalScrollTime)
                                       : 0;
            Uint32 baseDelay =
                m_waitingForInitialHorizontalRepeat ? SCROLL_INITIAL_DELAY_MS : SCROLL_REPEAT_DELAY_MS;
            if (m_thumbnailView)
            {
                baseDelay *= THUMBNAIL_SCROLL_DELAY_FACTOR;
            }
            const Uint32 targetDelay = baseDelay;

            if (m_lastHorizontalScrollTime == 0 || elapsed >= targetDelay)
            {
                if (m_leftHeld)
                {
                    moveSelectionHorizontal(-1);
                }
                else if (m_rightHeld)
                {
                    moveSelectionHorizontal(1);
                }

                m_lastHorizontalScrollTime = currentTime;
                m_waitingForInitialHorizontalRepeat = false;
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
    bool preserveThumbnails = !m_selectedFile.empty();
    cleanup(preserveThumbnails);

    // Flush any remaining SDL events to prevent stale input
    SDL_Event event;
    int flushedEvents = 0;
    while (SDL_PollEvent(&event))
    {
        flushedEvents++;
#ifdef TG5040_PLATFORM
        if (m_powerMessageEventType != 0 && event.type == m_powerMessageEventType)
        {
            delete static_cast<std::string*>(event.user.data1);
            continue;
        }
#endif

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

    // Create title with current directory
    std::string windowTitle = "SDLReader - " + m_currentPath;
    if (nk_begin(m_ctx, windowTitle.c_str(), nk_rect(0.0f, 0.0f, windowWidthF, windowHeightF),
                 NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE))
    {
        const float fontHeight = (m_ctx->style.font && m_ctx->style.font->height > 0.0f)
                                     ? m_ctx->style.font->height
                                     : 22.0f;
        const float helpTextHeight = std::max(24.0f, fontHeight + 4.0f);
        const float statusBarHeight = std::max(40.0f, fontHeight + 16.0f);
        const float statusBottomPadding = std::max(12.0f, m_ctx->style.window.padding.y);
        const float reservedHeight = 35.0f + helpTextHeight + statusBarHeight + 24.0f + statusBottomPadding;
        const float contentHeight = std::max(60.0f, windowHeightF - reservedHeight);

        nk_layout_row_dynamic(m_ctx, helpTextHeight, 1);
        nk_label_colored(m_ctx, "D-Pad: Navigate | A: Select | B: Back | X: Toggle View | Menu: Quit",
                         NK_TEXT_LEFT, nk_rgb(180, 180, 180));

        if (m_thumbnailView)
        {
            renderThumbnailViewNuklear(contentHeight, windowWidth);
        }
        else
        {
            renderListViewNuklear(contentHeight, windowWidth);
            m_gridColumns = 1;
        }

        // Bottom status bar (no spacing before it) - height scales with current font
        // Center text vertically by using equal top and bottom padding
        nk_layout_row_dynamic(m_ctx, statusBarHeight, 1);
        const float statusTextPaddingY = std::max(6.0f, (statusBarHeight - fontHeight) * 0.5f);
        const struct nk_vec2 statusTextPadding = nk_vec2(m_ctx->style.text.padding.x, statusTextPaddingY);
        const nk_bool pushedStatusPadding = nk_style_push_vec2(m_ctx, &m_ctx->style.text.padding, statusTextPadding);
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
        if (pushedStatusPadding)
        {
            nk_style_pop_vec2(m_ctx);
        }

        nk_layout_row_dynamic(m_ctx, statusBottomPadding, 1);
        nk_spacing(m_ctx, 1);

#ifdef TG5040_PLATFORM
        renderPowerMessageOverlay(windowWidthF, windowHeightF);
#endif
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

#ifdef TG5040_PLATFORM
void FileBrowser::showPowerMessage(const std::string& message)
{
    m_powerMessage = message;
    m_powerMessageStart = SDL_GetTicks();
}

void FileBrowser::renderPowerMessageOverlay(float windowWidth, float windowHeight)
{
    if (!m_ctx)
    {
        return;
    }

    if (m_powerMessage.empty())
    {
        return;
    }

    const Uint32 now = SDL_GetTicks();
    const std::string messageCopy = m_powerMessage;
    if ((now - m_powerMessageStart) >= POWER_MESSAGE_DURATION_MS)
    {
        m_powerMessage.clear();
        return;
    }

    struct nk_command_buffer* canvas = nk_window_get_canvas(m_ctx);
    if (!canvas || !m_ctx->style.font)
    {
        return;
    }

    const float overlayWidth = std::min(windowWidth * 0.85f, windowWidth - 60.0f);
    const float overlayHeight = std::max(110.0f, (m_ctx->style.font->height * 2.5f) + 30.0f);
    const float overlayX = (windowWidth - overlayWidth) * 0.5f;
    const float overlayY = (windowHeight - overlayHeight) * 0.5f;
    const struct nk_rect overlayRect = nk_rect(overlayX, overlayY, overlayWidth, overlayHeight);

    const struct nk_color bgColor = nk_rgba(180, 0, 0, 220);
    const struct nk_color borderColor = nk_rgba(255, 255, 255, 255);
    const struct nk_color textColor = nk_rgba(255, 255, 255, 255);

    nk_fill_rect(canvas, overlayRect, 6.0f, bgColor);
    nk_stroke_rect(canvas, overlayRect, 6.0f, 2.0f, borderColor);

    const struct nk_user_font* font = m_ctx->style.font;
    const float textAreaWidth = overlayRect.w - 40.0f;
    const struct nk_rect textRect = nk_rect(overlayRect.x + 20.0f,
                                            overlayRect.y + (overlayRect.h - font->height) * 0.5f,
                                            textAreaWidth,
                                            font->height);

    nk_draw_text(canvas, textRect, messageCopy.c_str(), static_cast<int>(messageCopy.size()), font,
                 nk_rgba(0, 0, 0, 0), textColor);
}
#endif

void FileBrowser::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_QUIT)
    {
        requestThumbnailShutdown();
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
            requestThumbnailShutdown();
            m_running = false;
            break;
        case SDLK_UP:
            if (!m_dpadUpHeld)
            {
                moveSelectionVertical(-1);
                m_dpadUpHeld = true;
                m_lastScrollTime = SDL_GetTicks();
                m_waitingForInitialRepeat = true;
            }
            break;
        case SDLK_DOWN:
            if (!m_dpadDownHeld)
            {
                moveSelectionVertical(1);
                m_dpadDownHeld = true;
                m_lastScrollTime = SDL_GetTicks();
                m_waitingForInitialRepeat = true;
            }
            break;
        case SDLK_LEFT:
            if (!m_leftHeld)
            {
                moveSelectionHorizontal(-1);
                m_leftHeld = true;
                m_lastHorizontalScrollTime = SDL_GetTicks();
                m_waitingForInitialHorizontalRepeat = true;
            }
            break;
        case SDLK_RIGHT:
            if (!m_rightHeld)
            {
                moveSelectionHorizontal(1);
                m_rightHeld = true;
                m_lastHorizontalScrollTime = SDL_GetTicks();
                m_waitingForInitialHorizontalRepeat = true;
            }
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
            m_waitingForInitialRepeat = false;
            break;
        case SDLK_DOWN:
            m_dpadDownHeld = false;
            m_lastScrollTime = 0;
            m_waitingForInitialRepeat = false;
            break;
        case SDLK_LEFT:
            m_leftHeld = false;
            if (!m_rightHeld)
            {
                m_lastHorizontalScrollTime = 0;
                m_waitingForInitialHorizontalRepeat = false;
            }
            break;
        case SDLK_RIGHT:
            m_rightHeld = false;
            if (!m_leftHeld)
            {
                m_lastHorizontalScrollTime = 0;
                m_waitingForInitialHorizontalRepeat = false;
            }
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
                m_waitingForInitialRepeat = true;
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (!m_dpadDownHeld)
            {
                moveSelectionVertical(1);
                m_dpadDownHeld = true;
                m_lastScrollTime = SDL_GetTicks();
                m_waitingForInitialRepeat = true;
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (!m_leftHeld)
            {
                moveSelectionHorizontal(-1);
                m_leftHeld = true;
                m_lastHorizontalScrollTime = SDL_GetTicks();
                m_waitingForInitialHorizontalRepeat = true;
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (!m_rightHeld)
            {
                moveSelectionHorizontal(1);
                m_rightHeld = true;
                m_lastHorizontalScrollTime = SDL_GetTicks();
                m_waitingForInitialHorizontalRepeat = true;
            }
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            jumpSelectionByLetter(1);
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            jumpSelectionByLetter(-1);
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
        case kToggleViewButton:
            toggleViewMode();
            break;
        case SDL_CONTROLLER_BUTTON_BACK:
        case SDL_CONTROLLER_BUTTON_START:
        case SDL_CONTROLLER_BUTTON_GUIDE:
            requestThumbnailShutdown();
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
            m_waitingForInitialRepeat = false;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            m_dpadDownHeld = false;
            m_lastScrollTime = 0;
            m_waitingForInitialRepeat = false;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            m_leftHeld = false;
            if (!m_rightHeld)
            {
                m_lastHorizontalScrollTime = 0;
                m_waitingForInitialHorizontalRepeat = false;
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            m_rightHeld = false;
            if (!m_leftHeld)
            {
                m_lastHorizontalScrollTime = 0;
                m_waitingForInitialHorizontalRepeat = false;
            }
            break;
        default:
            break;
        }
        break;

    case SDL_CONTROLLERAXISMOTION:
    {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
        {
            m_leftStickX = event.caxis.value;
        }
        else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
        {
            m_leftStickY = event.caxis.value;
        }
        else
        {
            break;
        }

        const Sint16 AXIS_DEAD_ZONE = 8000;
        const bool upActive = m_leftStickY < -AXIS_DEAD_ZONE;
        const bool downActive = m_leftStickY > AXIS_DEAD_ZONE;
        const bool leftActive = m_leftStickX < -AXIS_DEAD_ZONE;
        const bool rightActive = m_leftStickX > AXIS_DEAD_ZONE;

        if (upActive && !m_dpadUpHeld)
        {
            moveSelectionVertical(-1);
            m_dpadUpHeld = true;
            m_lastScrollTime = SDL_GetTicks();
            m_waitingForInitialRepeat = true;
        }
        else if (!upActive && m_dpadUpHeld)
        {
            m_dpadUpHeld = false;
            m_lastScrollTime = 0;
            m_waitingForInitialRepeat = false;
        }

        if (downActive && !m_dpadDownHeld)
        {
            moveSelectionVertical(1);
            m_dpadDownHeld = true;
            m_lastScrollTime = SDL_GetTicks();
            m_waitingForInitialRepeat = true;
        }
        else if (!downActive && m_dpadDownHeld)
        {
            m_dpadDownHeld = false;
            m_lastScrollTime = 0;
            m_waitingForInitialRepeat = false;
        }

        if (leftActive && !m_leftHeld)
        {
            moveSelectionHorizontal(-1);
            m_leftHeld = true;
            m_lastHorizontalScrollTime = SDL_GetTicks();
            m_waitingForInitialHorizontalRepeat = true;
        }
        else if (!leftActive && m_leftHeld)
        {
            m_leftHeld = false;
            if (!m_rightHeld)
            {
                m_lastHorizontalScrollTime = 0;
                m_waitingForInitialHorizontalRepeat = false;
            }
        }

        if (rightActive && !m_rightHeld)
        {
            moveSelectionHorizontal(1);
            m_rightHeld = true;
            m_lastHorizontalScrollTime = SDL_GetTicks();
            m_waitingForInitialHorizontalRepeat = true;
        }
        else if (!rightActive && m_rightHeld)
        {
            m_rightHeld = false;
            if (!m_leftHeld)
            {
                m_lastHorizontalScrollTime = 0;
                m_waitingForInitialHorizontalRepeat = false;
            }
        }

        break;
    }

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
        requestThumbnailShutdown();
        m_restoreSelectionPath = normalizePath(entry.fullPath).string();
        m_restoreSelectionPending = true;
        m_selectedFile = entry.fullPath;
        m_running = false;
    }
}

bool FileBrowser::s_lastThumbnailView = false;

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
    style->window.scrollbar_size = nk_vec2(18.0f, 18.0f);

    style->text.color = nk_rgb(235, 235, 235);

    style->button.normal = nk_style_item_color(nk_rgb(52, 58, 68));
    style->button.hover = nk_style_item_color(nk_rgb(70, 78, 92));
    style->button.active = nk_style_item_color(nk_rgb(83, 93, 110));
    style->button.border_color = nk_rgb(30, 30, 35);
    style->button.text_normal = nk_rgb(235, 235, 235);
    style->button.text_hover = nk_rgb(255, 255, 255);
    style->button.text_active = nk_rgb(255, 255, 255);

    style->scrollv.normal = nk_style_item_color(nk_rgba(80, 86, 104, 235));
    style->scrollv.hover = nk_style_item_color(nk_rgba(94, 102, 123, 245));
    style->scrollv.active = nk_style_item_color(nk_rgba(110, 120, 142, 255));
    style->scrollv.border = 1.0f;
    style->scrollv.rounding = 4.0f;
    style->scrollv.border_color = nk_rgba(15, 15, 18, 255);
    style->scrollv.cursor_normal = nk_style_item_color(nk_rgba(196, 208, 230, 255));
    style->scrollv.cursor_hover = nk_style_item_color(nk_rgba(214, 224, 242, 255));
    style->scrollv.cursor_active = nk_style_item_color(nk_rgba(228, 236, 251, 255));
    style->scrollv.border_cursor = 1.0f;
    style->scrollv.rounding_cursor = 4.0f;
    style->scrollv.cursor_border_color = nk_rgba(10, 10, 12, 255);
    style->scrollv.padding = nk_vec2(2.0f, 2.0f);

    style->scrollh = style->scrollv;
}

void FileBrowser::ensureSelectionVisible(float itemHeight, float viewHeight, float itemSpacing,
                                         float& scrollY, int& lastEnsureIndex, int targetIndex, int totalItems)
{
    if (targetIndex < 0 || totalItems <= 0)
    {
        return;
    }

    targetIndex = std::min(targetIndex, totalItems - 1);

    const float clampedItemHeight = std::max(0.0f, itemHeight);
    const float spacing = std::max(0.0f, itemSpacing);
    const float clampedViewHeight = std::max(viewHeight, clampedItemHeight);
    const float stride = clampedItemHeight + spacing;

    const float totalHeight =
        clampedItemHeight * static_cast<float>(totalItems) + spacing * static_cast<float>(std::max(0, totalItems - 1));
    if (totalHeight <= clampedViewHeight)
    {
        scrollY = 0.0f;
        lastEnsureIndex = targetIndex;
        return;
    }

    const float targetTop = stride * static_cast<float>(targetIndex);
    const float targetBottom = targetTop + clampedItemHeight;

    // Calculate the visible range
    const float visibleTop = scrollY;
    const float visibleBottom = scrollY + clampedViewHeight;

    // Check if item is going off the top
    if (targetTop < visibleTop)
    {
        // Scroll up by exactly the amount needed to show the item
        scrollY = targetTop;
    }
    // Check if item is going off the bottom (scroll before it disappears)
    else if (targetBottom > visibleBottom)
    {
        // Scroll down to bring the item into view at the bottom
        scrollY = targetBottom - clampedViewHeight;
    }

    const float maxScroll = std::max(0.0f, totalHeight - clampedViewHeight);
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);
    lastEnsureIndex = targetIndex;
}

void FileBrowser::renderListViewNuklear(float viewHeight, int windowWidth)
{
    (void) windowWidth;

    if (!m_ctx)
    {
        return;
    }

    const struct nk_user_font* font = m_ctx->style.font;
    if (!font)
    {
        return;
    }

    const float baseItemHeight = font->height + 14.0f;
    const float itemHeight = std::max(40.0f, baseItemHeight);
    const float clampedViewHeight = std::max(itemHeight, viewHeight);
    // Account for Nuklear's internal padding and borders - reduce effective height
    const float effectiveViewHeight = clampedViewHeight - itemHeight;
    const float rowSpacing = (m_ctx && m_ctx->style.window.spacing.y > 0.0f)
                                 ? m_ctx->style.window.spacing.y
                                 : 0.0f;

    const int totalItems = static_cast<int>(m_entries.size());
    const int clampedSelectedIndex = (totalItems > 0)
                                         ? std::clamp(m_selectedIndex, 0, totalItems - 1)
                                         : -1;
    // Check if we need to update scroll position for newly selected item
    bool needScrollUpdate = m_pendingListEnsure || (m_lastListEnsureIndex != clampedSelectedIndex);
    if (needScrollUpdate)
    {
        if (clampedSelectedIndex >= 0)
        {
            ensureSelectionVisible(itemHeight, effectiveViewHeight, rowSpacing, m_listScrollY,
                                   m_lastListEnsureIndex, clampedSelectedIndex, totalItems);
        }
        m_pendingListEnsure = false;
    }

    nk_layout_row_dynamic(m_ctx, clampedViewHeight, 1);

    if (nk_group_begin(m_ctx, "FileList", NK_WINDOW_BORDER | NK_WINDOW_SCROLL_AUTO_HIDE))
    {
        // Set the scroll position immediately after group begins if needed
        if (needScrollUpdate)
        {
            nk_group_set_scroll(m_ctx, "FileList", 0, static_cast<nk_uint>(m_listScrollY));
        }

        nk_layout_row_dynamic(m_ctx, itemHeight, 1);

        struct nk_command_buffer* canvas = &m_ctx->current->buffer;
        if (!canvas)
        {
            nk_group_end(m_ctx);
            return;
        }

        auto buildBadgeText = [](const FileEntry& entry) -> std::string
        {
            if (entry.isDirectory)
            {
                return entry.isParentLink ? "UP" : "DIR";
            }

            const size_t dotPos = entry.name.find_last_of('.');
            if (dotPos == std::string::npos || dotPos + 1 >= entry.name.size())
            {
                return "FILE";
            }

            std::string ext = entry.name.substr(dotPos + 1);
            if (ext.size() > 4)
            {
                ext.resize(4);
            }
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char ch)
                           { return static_cast<char>(std::toupper(ch)); });
            return ext;
        };

        enum class EntryCategory
        {
            ParentDirectory,
            Directory,
            Pdf,
            Comic,
            Epub,
            Mobi,
            Other
        };

        auto classifyEntry = [](const FileEntry& entry) -> EntryCategory
        {
            if (entry.isDirectory)
            {
                return entry.isParentLink ? EntryCategory::ParentDirectory : EntryCategory::Directory;
            }

            std::string lower = entry.name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });

            const size_t dotPos = lower.find_last_of('.');
            if (dotPos == std::string::npos)
            {
                return EntryCategory::Other;
            }

            const std::string ext = lower.substr(dotPos);
            if (ext == ".pdf")
            {
                return EntryCategory::Pdf;
            }
            if (ext == ".cbz" || ext == ".cbr" || ext == ".rar" || ext == ".zip")
            {
                return EntryCategory::Comic;
            }
            if (ext == ".epub")
            {
                return EntryCategory::Epub;
            }
            if (ext == ".mobi")
            {
                return EntryCategory::Mobi;
            }
            return EntryCategory::Other;
        };

        struct EntryStyleColors
        {
            nk_color accent;
            nk_color glyphPrimary;
            nk_color glyphSecondary;
            nk_color badgeBg;
            nk_color badgeText;
        };

        auto styleForCategory = [](EntryCategory category) -> EntryStyleColors
        {
            switch (category)
            {
            case EntryCategory::ParentDirectory:
                return {nk_rgb(255, 185, 135),
                        nk_rgba(255, 205, 165, 255),
                        nk_rgba(255, 225, 195, 255),
                        nk_rgba(255, 205, 170, 255),
                        nk_rgb(90, 55, 30)};
            case EntryCategory::Directory:
                return {nk_rgb(255, 210, 90),
                        nk_rgba(255, 200, 90, 255),
                        nk_rgba(255, 230, 150, 255),
                        nk_rgba(255, 210, 110, 255),
                        nk_rgb(60, 40, 20)};
            case EntryCategory::Pdf:
                return {nk_rgb(220, 85, 70),
                        nk_rgba(200, 70, 70, 255),
                        nk_rgba(255, 180, 150, 255),
                        nk_rgba(210, 70, 60, 255),
                        nk_rgb(255, 245, 245)};
            case EntryCategory::Comic:
                return {nk_rgb(175, 120, 255),
                        nk_rgba(135, 100, 220, 255),
                        nk_rgba(225, 205, 255, 255),
                        nk_rgba(170, 130, 245, 255),
                        nk_rgb(250, 250, 255)};
            case EntryCategory::Epub:
                return {nk_rgb(105, 200, 140),
                        nk_rgba(85, 150, 110, 255),
                        nk_rgba(175, 225, 185, 255),
                        nk_rgba(115, 195, 145, 255),
                        nk_rgb(30, 45, 30)};
            case EntryCategory::Mobi:
                return {nk_rgb(95, 185, 200),
                        nk_rgba(80, 150, 165, 255),
                        nk_rgba(190, 230, 235, 255),
                        nk_rgba(100, 190, 205, 255),
                        nk_rgb(25, 45, 55)};
            case EntryCategory::Other:
            default:
                return {nk_rgb(90, 140, 255),
                        nk_rgba(120, 150, 210, 255),
                        nk_rgba(210, 225, 255, 255),
                        nk_rgba(88, 120, 200, 255),
                        nk_rgb(235, 240, 255)};
            }
        };

        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            bool isSelected = (static_cast<int>(i) == m_selectedIndex);
            const FileEntry& entry = m_entries[i];
            const bool isParentLink = entry.isDirectory && entry.isParentLink;
            const bool evenRow = ((i % 2) == 0);
            const EntryCategory entryCategory = classifyEntry(entry);
            const EntryStyleColors entryColors = styleForCategory(entryCategory);

            std::string displayName = entry.name;
            if (displayName.empty())
            {
                displayName = entry.isDirectory ? "Folder" : "File";
            }
            if (isParentLink)
            {
                displayName = "Parent directory";
            }
            else if (entry.isDirectory && displayName.back() != '/')
            {
                displayName.push_back('/');
            }

            struct nk_style_button backup = m_ctx->style.button;
            m_ctx->style.button.normal = nk_style_item_color(nk_rgba(0, 0, 0, 0));
            m_ctx->style.button.hover = nk_style_item_color(nk_rgba(0, 0, 0, 0));
            m_ctx->style.button.active = nk_style_item_color(nk_rgba(0, 0, 0, 0));
            m_ctx->style.button.border = 0.0f;
            m_ctx->style.button.padding = nk_vec2(0.0f, 0.0f);
            m_ctx->style.button.text_alignment = NK_TEXT_LEFT;

            struct nk_rect rowBounds = nk_widget_bounds(m_ctx);
            const bool hovered = nk_widget_is_hovered(m_ctx) != 0;
            struct nk_rect background = rowBounds;
            background.x += 6.0f;
            background.y += 2.0f;
            background.w = std::max(0.0f, background.w - 12.0f);
            background.h = std::max(0.0f, background.h - 4.0f);

            const nk_color evenColor = nk_rgba(44, 46, 58, 255);
            const nk_color oddColor = nk_rgba(38, 40, 52, 255);
            const nk_color hoverColor = nk_rgba(56, 64, 86, 255);
            const nk_color selectedColor = nk_rgba(74, 100, 168, 255);

            nk_fill_rect(canvas, background, 6.0f, isSelected ? selectedColor : (evenRow ? evenColor : oddColor));

            nk_bool pressed = nk_button_label(m_ctx, "");
            if (hovered && !isSelected)
            {
                nk_fill_rect(canvas, background, 6.0f, hoverColor);
            }

            m_ctx->style.button = backup;

            if (pressed)
            {
                m_selectedIndex = static_cast<int>(i);
                m_selectedFile.clear();
                resetSelectionScrollTargets();
                isSelected = true;
            }

            const nk_color accentColor = entryColors.accent;
            struct nk_rect accent = background;
            accent.w = 4.0f;
            nk_fill_rect(canvas, accent, 2.0f, accentColor);

            const float iconSize = std::max(18.0f, itemHeight * 0.45f);
            struct nk_rect iconRect = background;
            iconRect.x += accent.w + 12.0f;
            iconRect.y = background.y + (background.h - iconSize) * 0.5f;
            iconRect.w = iconSize;
            iconRect.h = iconSize;

            if (entry.isDirectory)
            {
                struct nk_rect tabRect = iconRect;
                tabRect.h *= 0.35f;
                tabRect.w *= 0.65f;
                nk_fill_rect(canvas, tabRect, 2.0f, entryColors.glyphSecondary);

                struct nk_rect bodyRect = iconRect;
                bodyRect.y += tabRect.h * 0.4f;
                bodyRect.h = iconRect.h - tabRect.h * 0.3f;
                nk_fill_rect(canvas, bodyRect, 3.5f, entryColors.glyphPrimary);
            }
            else
            {
                nk_fill_rect(canvas, iconRect, 3.5f, entryColors.glyphPrimary);

                const float fold = iconRect.w * 0.32f;
                const float foldX = iconRect.x + iconRect.w - fold;
                nk_fill_triangle(canvas,
                                 foldX, iconRect.y,
                                 iconRect.x + iconRect.w, iconRect.y + fold,
                                 iconRect.x + iconRect.w, iconRect.y,
                                 entryColors.glyphSecondary);
                nk_stroke_line(canvas,
                               foldX, iconRect.y,
                               iconRect.x + iconRect.w, iconRect.y + fold, 1.0f, nk_rgba(255, 255, 255, 80));
            }

            const float textStart = iconRect.x + iconRect.w + 12.0f;
            const float contentRight = background.x + background.w - 12.0f;

            std::string badgeText = buildBadgeText(entry);
            float badgeWidth = 0.0f;
            float badgeHeight = font->height + 4.0f;
            struct nk_rect badgeRect = {0, 0, 0, 0};
            bool showBadge = !badgeText.empty();
            if (showBadge)
            {
                const float badgeTextWidth =
                    font->width(font->userdata, font->height, badgeText.c_str(), static_cast<int>(badgeText.size()));
                badgeWidth = badgeTextWidth + 20.0f;
                badgeHeight = std::min(badgeHeight, background.h - 4.0f);

                const float availableWidth = contentRight - textStart;
                if (availableWidth > 180.0f)
                {
                    badgeRect.w = badgeWidth;
                    badgeRect.h = badgeHeight;
                    badgeRect.x = contentRight - badgeRect.w;
                    badgeRect.y = background.y + (background.h - badgeRect.h) * 0.5f;
                }
                else
                {
                    showBadge = false;
                }
            }

            const float textRight = showBadge ? (badgeRect.x - 12.0f) : contentRight;
            const float maxTextWidth = std::max(0.0f, textRight - textStart);

            if (maxTextWidth > 0.0f)
            {
                std::string truncatedName = truncateToWidth(displayName, maxTextWidth, font);
                nk_color textColor;
                if (isSelected)
                {
                    textColor = nk_rgb(255, 255, 255);
                }
                else if (entry.isDirectory)
                {
                    textColor = isParentLink ? nk_rgb(255, 220, 185) : nk_rgb(255, 233, 180);
                }
                else
                {
                    textColor = nk_rgb(230, 235, 255);
                }

                struct nk_rect textRect;
                textRect.x = textStart;
                textRect.w = maxTextWidth;
                textRect.h = font->height;
                textRect.y = background.y + (background.h - textRect.h) * 0.5f;
                nk_draw_text(canvas, textRect, truncatedName.c_str(), static_cast<int>(truncatedName.size()), font,
                             nk_rgba(0, 0, 0, 0), textColor);
            }

            if (showBadge)
            {
                nk_color badgeBg = entryColors.badgeBg;
                nk_color badgeTextColor = entryColors.badgeText;

                nk_fill_rect(canvas, badgeRect, badgeRect.h * 0.5f, badgeBg);

                struct nk_rect badgeTextRect = badgeRect;
                badgeTextRect.x += 10.0f;
                badgeTextRect.w = badgeRect.w - 20.0f;
                nk_draw_text(canvas, badgeTextRect, badgeText.c_str(), static_cast<int>(badgeText.size()), font,
                             nk_rgba(0, 0, 0, 0), badgeTextColor);
            }
        }

        // Only read back scroll position if we didn't just set it
        if (!needScrollUpdate && totalItems > 0)
        {
            nk_uint scrollX = 0;
            nk_uint scrollY = 0;
            nk_group_get_scroll(m_ctx, "FileList", &scrollX, &scrollY);
            m_listScrollY = static_cast<float>(scrollY);
        }
        nk_group_end(m_ctx);
    }
}

void FileBrowser::renderThumbnailViewNuklear(float viewHeight, int windowWidth)
{
    if (!m_ctx || !m_ctx->current || !m_ctx->current->layout)
    {
        return;
    }

    struct nk_window* win = m_ctx->current;

    const float baseThumb = static_cast<float>(THUMBNAIL_MAX_DIM);
    const float labelHeight = 40.0f;
    const float tilePadding = 12.0f;
    const float maxBorderThickness = 4.0f;
    const float thumbnailAreaHeight = baseThumb;
    const float tileHeight = thumbnailAreaHeight + labelHeight + 2.0f * (tilePadding + maxBorderThickness);

    const float spacingX = (m_ctx->style.window.spacing.x > 0.0f) ? m_ctx->style.window.spacing.x : 0.0f;
    const float paddingX = m_ctx->style.window.padding.x * 2.0f;
    const float scrollbarWidth = (m_ctx->style.window.scrollbar_size.x > 0.0f) ? m_ctx->style.window.scrollbar_size.x : 16.0f;
    const float extraScrollbarPadding = scrollbarWidth + spacingX + 12.0f;
    const float usableWidth = std::max(0.0f, static_cast<float>(windowWidth) - paddingX - extraScrollbarPadding);
    const float preferredTileWidth = baseThumb + 48.0f;
    const int desiredColumns = 4;
    const int maxColumnsFit = std::max(1, static_cast<int>((usableWidth + spacingX) / (preferredTileWidth + spacingX)));
    int columns = (maxColumnsFit >= desiredColumns) ? desiredColumns : maxColumnsFit;
    columns = std::max(1, columns);
    float totalWidthForTiles = usableWidth - spacingX * static_cast<float>(columns - 1);
    if (totalWidthForTiles <= 0.0f)
    {
        totalWidthForTiles = usableWidth;
    }
    float tileWidth = totalWidthForTiles / static_cast<float>(columns);
    tileWidth = std::max(96.0f, tileWidth - 1.0f);

    const struct nk_user_font* font = m_ctx->style.font;
    if (!font)
    {
        return;
    }

    m_gridColumns = std::max(1, columns);
    const float clampedViewHeight = std::max(tileHeight + 20.0f, viewHeight);
    const float verticalPadding = (m_ctx->style.window.padding.y > 0.0f)
                                      ? m_ctx->style.window.padding.y * 2.0f
                                      : 0.0f;
    // Subtract the surrounding padding so the last tile remains fully visible when scrolled to the bottom.
    const float effectiveViewHeight = std::max(tileHeight, clampedViewHeight - verticalPadding);
    const float rowSpacing = (m_ctx && m_ctx->style.window.spacing.y > 0.0f)
                                 ? m_ctx->style.window.spacing.y
                                 : 0.0f;

    const int totalEntries = static_cast<int>(m_entries.size());
    const int totalRows = (m_gridColumns > 0)
                              ? (totalEntries + m_gridColumns - 1) / m_gridColumns
                              : 0;
    const int currentRow = (totalEntries > 0 && m_gridColumns > 0)
                               ? std::clamp(m_selectedIndex, 0, totalEntries - 1) / m_gridColumns
                               : -1;

    // Only update scroll if pending flag is set (from resetSelectionScrollTargets)
    // Don't update just because currentRow changed - that can happen due to layout changes
    bool needScrollUpdate = m_pendingThumbEnsure;

    bool needsScroll = false;       // Track if we actually need to change scroll
    bool shouldApplyScroll = false; // Track when we have to push cached scroll into Nuklear

    if (needScrollUpdate)
    {
        if (currentRow >= 0 && totalRows > 0)
        {
            // Calculate scroll position based on row layout
            const float stride = tileHeight + rowSpacing;
            const float totalHeight = tileHeight * static_cast<float>(totalRows) +
                                      rowSpacing * static_cast<float>(std::max(0, totalRows - 1));

            // Only scroll if content is larger than view
            if (totalHeight > effectiveViewHeight)
            {
                const float rowTop = stride * static_cast<float>(currentRow);
                const float rowBottom = rowTop + tileHeight;

                float desiredScrollY = m_thumbnailScrollY;

                // Check if row is above visible area
                if (rowTop < m_thumbnailScrollY)
                {
                    desiredScrollY = rowTop;
                    needsScroll = true;
                }
                // Check if row is below visible area
                else if (rowBottom > m_thumbnailScrollY + effectiveViewHeight)
                {
                    desiredScrollY = rowBottom - effectiveViewHeight;
                    needsScroll = true;
                }

                // Clamp to valid range
                const float maxScroll = std::max(0.0f, totalHeight - effectiveViewHeight);
                desiredScrollY = std::clamp(desiredScrollY, 0.0f, maxScroll);

                // Only update scroll if the row is not already fully visible
                if (needsScroll)
                {
                    m_thumbnailScrollY = desiredScrollY;
                }
            }
            else
            {
                m_thumbnailScrollY = 0.0f;
            }

            m_lastThumbEnsureIndex = currentRow;
            shouldApplyScroll = true;
        }
        m_pendingThumbEnsure = false;
        // When we restored selection after opening a document Nuklear's scroll state is fresh,
        // so we still need to reapply whatever value we think is current even if it already
        // contains the row. shouldApplyScroll keeps track of that requirement.
    }

    nk_layout_row_dynamic(m_ctx, clampedViewHeight, 1);

    if (nk_group_begin(m_ctx, "ThumbnailGrid", NK_WINDOW_BORDER | NK_WINDOW_SCROLL_AUTO_HIDE))
    {
        if (shouldApplyScroll)
        {
            nk_group_set_scroll(m_ctx, "ThumbnailGrid", 0, static_cast<nk_uint>(m_thumbnailScrollY));

            // Check what Nuklear actually set it to
            nk_uint checkX = 0, checkY = 0;
            nk_group_get_scroll(m_ctx, "ThumbnailGrid", &checkX, &checkY);
            // Keep cached scroll in sync so we don't try to scroll back to an already visible row.
            m_thumbnailScrollY = static_cast<float>(checkY);
            m_scrollJustSet = true;
        }

        if (!m_entries.empty())
        {
            auto centerRect = [](const struct nk_rect& area, int texWidth, int texHeight) -> struct nk_rect
            {
                struct nk_rect rect = area;
                if (texWidth <= 0 || texHeight <= 0 || area.w <= 0.0f || area.h <= 0.0f)
                {
                    return rect;
                }
                const float aspect = static_cast<float>(texWidth) / static_cast<float>(texHeight);
                const float areaAspect = area.w / area.h;
                if (areaAspect > aspect)
                {
                    rect.h = area.h;
                    rect.w = rect.h * aspect;
                }
                else
                {
                    rect.w = area.w;
                    rect.h = rect.w / aspect;
                }
                rect.x = area.x + (area.w - rect.w) * 0.5f;
                rect.y = area.y + (area.h - rect.h) * 0.5f;
                return rect;
            };

            auto drawCenteredText = [&](const struct nk_rect& bounds, const std::string& text, nk_color color)
            {
                if (!font || text.empty())
                {
                    return;
                }
                const float measuredWidth = font->width(font->userdata, font->height, text.c_str(),
                                                        static_cast<int>(text.size()));
                struct nk_rect textRect = bounds;
                const float clampedWidth = std::min(bounds.w, measuredWidth);
                textRect.w = clampedWidth;
                textRect.x = bounds.x + std::max(0.0f, (bounds.w - clampedWidth) * 0.5f);
                textRect.y = bounds.y + std::max(0.0f, (bounds.h - font->height) * 0.5f);
                textRect.h = font->height;
                nk_draw_text(&win->buffer, textRect, text.c_str(), static_cast<int>(text.size()),
                             font, nk_rgba(0, 0, 0, 0), color);
            };

            for (size_t i = 0; i < m_entries.size(); ++i)
            {
                // Start a new row at the beginning and after every 'columns' items
                if ((i % columns) == 0)
                {
                    nk_layout_row_dynamic(m_ctx, tileHeight, columns);
                }

                struct nk_style_button tileButtonStyle = m_ctx->style.button;
                m_ctx->style.button.normal = nk_style_item_color(nk_rgba(0, 0, 0, 0));
                m_ctx->style.button.hover = nk_style_item_color(nk_rgba(0, 0, 0, 0));
                m_ctx->style.button.active = nk_style_item_color(nk_rgba(0, 0, 0, 0));
                m_ctx->style.button.border = 0.0f;
                m_ctx->style.button.rounding = 0.0f;
                m_ctx->style.button.text_normal = nk_rgba(0, 0, 0, 0);
                m_ctx->style.button.text_hover = nk_rgba(0, 0, 0, 0);
                m_ctx->style.button.text_active = nk_rgba(0, 0, 0, 0);
                m_ctx->style.button.padding = nk_vec2(0.0f, 0.0f);

                struct nk_rect tileBounds = nk_widget_bounds(m_ctx);
                const bool hovered = nk_widget_is_hovered(m_ctx) != 0;
                nk_bool pressed = nk_button_label(m_ctx, "");
                m_ctx->style.button = tileButtonStyle;

                const FileEntry& entry = m_entries[i];
                ThumbnailData& thumb = getOrCreateThumbnail(entry);
                const bool isSelected = (static_cast<int>(i) == m_selectedIndex);
                const bool isParentLink = entry.isDirectory && entry.isParentLink;
                const bool isRegularDirectory = entry.isDirectory && !entry.isParentLink;

                if (pressed)
                {
                    m_selectedIndex = static_cast<int>(i);
                    m_selectedFile.clear();
                    resetSelectionScrollTargets();
                }

                const float borderThickness = isSelected ? 4.0f : 2.0f;
                const nk_color baseBorderColor = isParentLink ? nk_rgb(255, 170, 120)
                                                              : (isRegularDirectory ? nk_rgb(215, 190, 100) : nk_rgb(70, 80, 95));
                const nk_color hoverBorderColor = isParentLink ? nk_rgb(255, 190, 140)
                                                               : (isRegularDirectory ? nk_rgb(235, 210, 130) : nk_rgb(120, 145, 200));
                const nk_color basePanelColor = isParentLink ? nk_rgba(60, 48, 40, 255)
                                                             : (isRegularDirectory ? nk_rgba(45, 48, 58, 255) : nk_rgba(34, 36, 42, 255));
                const nk_color hoverPanelColor = isParentLink ? nk_rgba(72, 58, 50, 255)
                                                              : (isRegularDirectory ? nk_rgba(52, 56, 66, 255) : nk_rgba(40, 42, 50, 255));

                const nk_color borderColor = isSelected ? nk_rgb(255, 210, 0)
                                                        : (hovered ? hoverBorderColor : baseBorderColor);
                const nk_color panelColor = isSelected ? nk_rgba(45, 55, 78, 255)
                                                       : (hovered ? hoverPanelColor : basePanelColor);

                struct nk_rect borderRect = tileBounds;
                borderRect.x += 1.0f;
                borderRect.y += 1.0f;
                borderRect.w = std::max(0.0f, borderRect.w - 2.0f);
                borderRect.h = std::max(0.0f, borderRect.h - 2.0f);

                nk_fill_rect(&win->buffer, borderRect, 8.0f, nk_rgba(15, 16, 20, 255));
                nk_stroke_rect(&win->buffer, borderRect, 8.0f, borderThickness, borderColor);

                struct nk_rect innerRect = borderRect;
                innerRect.x += borderThickness;
                innerRect.y += borderThickness;
                innerRect.w = std::max(0.0f, innerRect.w - borderThickness * 2.0f);
                innerRect.h = std::max(0.0f, innerRect.h - borderThickness * 2.0f);
                nk_fill_rect(&win->buffer, innerRect, 6.0f, panelColor);

                struct nk_rect contentRect = innerRect;
                contentRect.x += tilePadding;
                contentRect.y += tilePadding;
                contentRect.w = std::max(0.0f, contentRect.w - tilePadding * 2.0f);
                contentRect.h = std::max(0.0f, contentRect.h - tilePadding * 2.0f);

                const float textAreaHeight = labelHeight;
                const float availableThumbHeight = std::max(24.0f, contentRect.h - textAreaHeight);
                struct nk_rect thumbRect = contentRect;
                thumbRect.h = availableThumbHeight;

                if (thumb.texture && thumb.width > 0 && thumb.height > 0)
                {
                    const struct nk_rect imageRect = centerRect(thumbRect, thumb.width, thumb.height);
                    struct nk_image img = nk_image_ptr(static_cast<void*>(thumb.texture.get()));
                    nk_draw_image(&win->buffer, imageRect, &img, nk_rgb(255, 255, 255));
                }
                else
                {
                    nk_color placeholderColor;
                    if (isParentLink)
                    {
                        placeholderColor = nk_rgba(90, 70, 55, 255);
                    }
                    else if (entry.isDirectory)
                    {
                        placeholderColor = nk_rgba(90, 80, 55, 255);
                    }
                    else
                    {
                        placeholderColor = nk_rgba(55, 60, 68, 255);
                    }
                    nk_fill_rect(&win->buffer, thumbRect, 4.0f, placeholderColor);

                    std::string placeholderText;
                    if (thumb.pending)
                    {
                        placeholderText = "Loading...";
                    }
                    else if (isParentLink)
                    {
                        placeholderText = "Parent";
                    }
                    else if (entry.isDirectory)
                    {
                        placeholderText = "Folder";
                    }
                    else
                    {
                        placeholderText = "No preview";
                    }
                    drawCenteredText(thumbRect, placeholderText, nk_rgb(220, 220, 220));
                }

                if (entry.isDirectory)
                {
                    struct nk_rect badgeRect = thumbRect;
                    badgeRect.x += 6.0f;
                    badgeRect.y += 6.0f;
                    badgeRect.w = std::max(0.0f, std::min(72.0f, thumbRect.w - 12.0f));
                    badgeRect.h = std::max(0.0f, std::min(24.0f, thumbRect.h - 12.0f));
                    if (badgeRect.w > 0.0f && badgeRect.h > 0.0f)
                    {
                        nk_color badgeColor = isParentLink ? nk_rgba(255, 160, 110, 220) : nk_rgba(255, 205, 80, 220);
                        nk_fill_rect(&win->buffer, badgeRect, 4.0f, badgeColor);
                        drawCenteredText(badgeRect, isParentLink ? "UP" : "DIR", nk_rgb(30, 30, 30));
                    }
                }

                struct nk_rect labelRect = contentRect;
                labelRect.y = contentRect.y + availableThumbHeight + 4.0f;
                labelRect.h = std::max(0.0f, textAreaHeight - 4.0f);

                std::string displayName = entry.name;
                if (displayName.empty())
                {
                    displayName = entry.isDirectory ? "Folder" : "File";
                }
                if (isParentLink)
                {
                    displayName = "[UP]..";
                }
                else if (entry.isDirectory && !displayName.empty())
                {
                    displayName.push_back('/');
                }
                displayName = truncateToWidth(displayName, labelRect.w, font);

                nk_color textColor = entry.isDirectory ? nk_rgb(255, 230, 160) : nk_rgb(235, 235, 235);
                if (isParentLink)
                {
                    textColor = nk_rgb(255, 205, 170);
                }
                if (isSelected)
                {
                    textColor = nk_rgb(255, 255, 255);
                }
                drawCenteredText(labelRect, displayName, textColor);
            }
        }

        // Don't read back scroll after programmatic setting - let Nuklear handle clamping
        // This prevents visual "bounce" when Nuklear clamps our scroll to valid range
        if (m_scrollJustSet)
        {
            m_scrollJustSet = false;
        }
        else
        {
            nk_uint scrollX = 0;
            nk_uint scrollY = 0;
            nk_group_get_scroll(m_ctx, "ThumbnailGrid", &scrollX, &scrollY);
            m_thumbnailScrollY = static_cast<float>(scrollY);
        }
        nk_group_end(m_ctx);
    }
}

void FileBrowser::resetSelectionScrollTargets()
{
    m_lastListEnsureIndex = -1;
    m_lastThumbEnsureIndex = -1;
    m_pendingListEnsure = true;
    m_pendingThumbEnsure = true;
}
