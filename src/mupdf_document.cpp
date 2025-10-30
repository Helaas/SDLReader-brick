#include "mupdf_document.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

struct MuPdfDocument::PageScaleInfo
{
    fz_display_list* displayList{nullptr};
    fz_rect bounds{};
    fz_matrix transform{};
    fz_irect bbox{};
    int width{0};
    int height{0};
    float baseScale{1.0f};
    float downsampleScale{1.0f};
};

MuPdfDocument::MuPdfDocument()
    : Document()
{
    // Initialize MuPDF context with limited store size to prevent corruption
    m_ctx = std::unique_ptr<fz_context, ContextDeleter>(fz_new_context(nullptr, nullptr, 256 << 20)); // 256MB
}

MuPdfDocument::~MuPdfDocument()
{
    std::cout.flush();

    m_asyncShutdown = true;
    cancelPrerendering();
    joinAsyncRenderThread();

    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache.clear();
        m_argbCache.clear();
        m_dimensionCache.clear();
    }

    resetDisplayCache();

    std::cout.flush();
}

bool MuPdfDocument::open(const std::string& filePath)
{
    return open(filePath, false);
}

// Internal version with context reuse option
bool MuPdfDocument::open(const std::string& filePath, bool reuseContexts)
{
    if (!reuseContexts)
    {
        close();
    }
    else
    {
        // Only close documents, keep contexts
        m_doc.reset();
        m_prerenderDoc.reset();
    }

    // Store file path for potential reopening
    m_filePath = filePath;

    fz_context* ctx = nullptr;
    if (!reuseContexts || !m_ctx)
    {
        ctx = fz_new_context(nullptr, nullptr, 256 << 20); // 256MB
        if (!ctx)
        {
            std::cerr << "Cannot create MuPDF context\n";
            return false;
        }
        m_ctx.reset(ctx);
        fz_register_document_handlers(ctx);
    }
    else
    {
        ctx = m_ctx.get();
    }

    // Apply user CSS before opening document if it was set
    if (!m_userCSS.empty())
    {
        fz_try(ctx)
        {
            fz_set_user_css(ctx, m_userCSS.c_str());
            std::cout << "Applied CSS before opening document: " << m_userCSS << std::endl;
        }
        fz_catch(ctx)
        {
            std::cerr << "Failed to set user CSS before opening: " << fz_caught_message(ctx) << std::endl;
        }
    }

    // Create separate context for prerendering to avoid race conditions
    fz_context* prerenderCtx = nullptr;
    if (!reuseContexts || !m_prerenderCtx)
    {
        prerenderCtx = fz_new_context(nullptr, nullptr, 256 << 20); // 256MB
        if (!prerenderCtx)
        {
            std::cerr << "Cannot create prerender MuPDF context\n";
            return false;
        }
        m_prerenderCtx.reset(prerenderCtx);
        fz_register_document_handlers(prerenderCtx);
    }
    else
    {
        prerenderCtx = m_prerenderCtx.get();
    }

    // Apply user CSS to prerender context as well
    if (!m_userCSS.empty())
    {
        fz_try(prerenderCtx)
        {
            fz_set_user_css(prerenderCtx, m_userCSS.c_str());
        }
        fz_catch(prerenderCtx)
        {
            std::cerr << "Failed to set user CSS in prerender context: " << fz_caught_message(prerenderCtx) << std::endl;
        }
    }

    fz_document* doc = nullptr;
    fz_var(doc);

    fz_try(ctx)
    {
        doc = fz_open_document(ctx, filePath.c_str());
    }
    fz_catch(ctx)
    {
        const char* fzError = fz_caught_message(ctx);
        std::string errorMsg = "Failed to open document: " + filePath;
        if (fzError && strlen(fzError) > 0)
        {
            errorMsg += " (MuPDF error: " + std::string(fzError) + ")";
        }
        std::cerr << errorMsg << "\n";
        return false;
    }

    m_doc = std::unique_ptr<fz_document, DocumentDeleter>(doc, DocumentDeleter{ctx});

    // Also open document in prerender context
    fz_document* prerenderDocPtr = nullptr;
    fz_var(prerenderDocPtr);

    fz_try(prerenderCtx)
    {
        prerenderDocPtr = fz_open_document(prerenderCtx, filePath.c_str());
    }
    fz_catch(prerenderCtx)
    {
        std::cerr << "Failed to open document in prerender context: " << filePath << "\n";
        return false;
    }

    m_prerenderDoc = std::unique_ptr<fz_document, DocumentDeleter>(prerenderDocPtr, DocumentDeleter{prerenderCtx});
    m_pageCount = fz_count_pages(ctx, doc);

    m_asyncShutdown = false;
    resetDisplayCache();

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_cache.clear();
        m_argbCache.clear();
    }

    return true;
}

bool MuPdfDocument::reopenWithCSS(const std::string& css)
{
    if (m_filePath.empty())
    {
        std::cerr << "Cannot reopen document: no file path stored" << std::endl;
        return false;
    }

    std::cout << "Reopening document with new CSS: " << css << std::endl;

    // Store the file path before any operations
    std::string savedPath = m_filePath;

    // Stop any background operations before we touch shared state
    cancelPrerendering();

    std::unique_lock<std::mutex> renderLock(m_renderMutex);
    std::unique_lock<std::mutex> prerenderLock(m_prerenderMutex);

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_cache.clear();
        m_argbCache.clear();
    }

    // Close existing documents but keep contexts alive to avoid TG5040 crash
    m_doc.reset();
    m_prerenderDoc.reset();

    // Update CSS on existing contexts
    m_userCSS = css;

    if (m_ctx)
    {
        fz_try(m_ctx.get())
        {
            fz_set_user_css(m_ctx.get(), css.c_str());
        }
        fz_catch(m_ctx.get())
        {
            std::cerr << "Failed to update CSS on main context: " << fz_caught_message(m_ctx.get()) << std::endl;
        }
    }

    if (m_prerenderCtx)
    {
        fz_try(m_prerenderCtx.get())
        {
            fz_set_user_css(m_prerenderCtx.get(), css.c_str());
        }
        fz_catch(m_prerenderCtx.get())
        {
            std::cerr << "Failed to update CSS on prerender context: " << fz_caught_message(m_prerenderCtx.get()) << std::endl;
        }
    }

    // Reopen documents using existing contexts (reuseContexts=true)
    bool result = open(savedPath, true);

    if (!result)
    {
        std::cerr << "Failed to reopen document with new CSS" << std::endl;
    }

    return result;
}

std::vector<unsigned char> MuPdfDocument::renderPage(int pageNumber, int& width, int& height, int zoom)
{
    std::lock_guard<std::mutex> renderLock(m_renderMutex);

    if (!m_ctx || !m_doc)
    {
        throw std::runtime_error("Document not open");
    }

    if (pageNumber < 0 || pageNumber >= m_pageCount)
    {
        throw std::runtime_error("Invalid page number: " + std::to_string(pageNumber));
    }

    auto key = std::make_pair(pageNumber, zoom);

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        auto it = m_cache.find(key);
        if (it != m_cache.end())
        {
            auto& [buffer, cachedWidth, cachedHeight] = it->second;
            width = cachedWidth;
            height = cachedHeight;
            return buffer;
        }
    }

    fz_context* ctx = m_ctx.get();
    if (!ctx)
    {
        throw std::runtime_error("MuPDF context is null");
    }

    ensureDisplayList(pageNumber);
    PageScaleInfo scaleInfo = computePageScaleInfoLocked(pageNumber, zoom);

    if (!scaleInfo.displayList)
    {
        throw std::runtime_error("Display list missing for page " + std::to_string(pageNumber));
    }

    fz_pixmap* pix = nullptr;
    fz_device* dev = nullptr;
    std::vector<unsigned char> buffer;

    fz_try(ctx)
    {
        pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), scaleInfo.bbox, nullptr, 0);
        if (!pix)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to allocate pixmap for page %d", pageNumber);
        }

        fz_clear_pixmap_with_value(ctx, pix, 0xFF);

        dev = fz_new_draw_device(ctx, fz_identity, pix);
        if (!dev)
        {
            fz_drop_pixmap(ctx, pix);
            pix = nullptr;
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to create draw device for page %d", pageNumber);
        }

        fz_run_display_list(ctx, scaleInfo.displayList, dev, scaleInfo.transform, scaleInfo.bounds, nullptr);
        fz_close_device(ctx, dev);
        fz_drop_device(ctx, dev);
        dev = nullptr;

        width = scaleInfo.width;
        height = scaleInfo.height;

        size_t dataSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
        buffer.resize(dataSize);
        memcpy(buffer.data(), fz_pixmap_samples(ctx, pix), dataSize);

        fz_drop_pixmap(ctx, pix);
        pix = nullptr;
    }
    fz_catch(ctx)
    {
        if (dev)
            fz_drop_device(ctx, dev);
        if (pix)
            fz_drop_pixmap(ctx, pix);

        const char* muErr = fz_caught_message(ctx);
        std::string message = "Error rendering page " + std::to_string(pageNumber);
        if (muErr && strlen(muErr) > 0)
        {
            message += ": ";
            message += muErr;
        }
        std::cerr << "MuPdfDocument: " << message << std::endl;
        throw std::runtime_error(message);
    }

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

        if (zoom > 200 && m_cache.size() > 10)
        {
            auto it = m_cache.begin();
            std::advance(it, static_cast<long>(m_cache.size() - 5));
            m_cache.erase(m_cache.begin(), it);
        }
        else if (m_cache.size() > 20)
        {
            auto it = m_cache.begin();
            std::advance(it, static_cast<long>(m_cache.size() - 15));
            m_cache.erase(m_cache.begin(), it);
        }

        m_cache[key] = std::make_tuple(buffer, width, height);
    }

    return buffer;
}

std::vector<uint32_t> MuPdfDocument::renderPageARGB(int pageNumber, int& width, int& height, int zoom)
{
    if (!m_ctx || !m_doc)
    {
        throw std::runtime_error("Document not open");
    }

    if (pageNumber < 0 || pageNumber >= m_pageCount)
    {
        throw std::runtime_error("Invalid page number: " + std::to_string(pageNumber));
    }

    auto key = std::make_pair(pageNumber, zoom);

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        auto it = m_argbCache.find(key);
        if (it != m_argbCache.end())
        {
            auto& [buffer, cachedWidth, cachedHeight] = it->second;
            width = cachedWidth;
            height = cachedHeight;
            return buffer;
        }
    }

    std::vector<unsigned char> rgbData = renderPage(pageNumber, width, height, zoom);

    std::vector<uint32_t> argbBuffer(width * height);
    const uint8_t* src = rgbData.data();
    uint32_t* dst = argbBuffer.data();
    for (int i = 0; i < width * height; ++i)
    {
        dst[i] = rgb24_to_argb32(src[i * 3], src[i * 3 + 1], src[i * 3 + 2]);
    }

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

#ifdef TG5040_PLATFORM
        if (m_argbCache.size() >= 2)
        {
#else
        if (m_argbCache.size() >= 5)
        {
#endif
            m_argbCache.erase(m_argbCache.begin());
        }

        m_argbCache[key] = std::make_tuple(argbBuffer, width, height);
    }

    return argbBuffer;
}

bool MuPdfDocument::tryGetCachedPageARGB(int pageNumber, int scale, std::vector<uint32_t>& buffer, int& width, int& height)
{
    auto key = std::make_pair(pageNumber, scale);

    std::vector<unsigned char> rgbCopy;

    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto argbIt = m_argbCache.find(key);
        if (argbIt != m_argbCache.end())
        {
            auto& [cachedBuffer, cachedWidth, cachedHeight] = argbIt->second;
            buffer = cachedBuffer;
            width = cachedWidth;
            height = cachedHeight;
            return true;
        }

        auto rgbIt = m_cache.find(key);
        if (rgbIt != m_cache.end())
        {
            rgbCopy = std::get<0>(rgbIt->second);
            width = std::get<1>(rgbIt->second);
            height = std::get<2>(rgbIt->second);
        }
        else
        {
            return false;
        }
    }

    if (rgbCopy.empty() || width <= 0 || height <= 0)
    {
        return false;
    }

    buffer.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    const unsigned char* src = rgbCopy.data();
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        buffer[i] = (0xFFu << 24) | (static_cast<uint32_t>(src[i * 3]) << 16) |
                    (static_cast<uint32_t>(src[i * 3 + 1]) << 8) | static_cast<uint32_t>(src[i * 3 + 2]);
    }

    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
#ifdef TG5040_PLATFORM
        if (m_argbCache.size() >= 2)
        {
#else
        if (m_argbCache.size() >= 5)
        {
#endif
            m_argbCache.erase(m_argbCache.begin());
        }

        m_argbCache[key] = std::make_tuple(buffer, width, height);
    }

    return true;
}

void MuPdfDocument::requestPageRenderAsync(int page, int scale)
{
    if (!m_ctx || !m_doc)
    {
        return;
    }

    if (m_asyncShutdown)
    {
        return;
    }

    auto key = std::make_pair(page, scale);

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        if (m_argbCache.find(key) != m_argbCache.end())
        {
            return;
        }
    }

    std::unique_lock<std::mutex> lock(m_asyncRenderMutex);
    if (m_asyncShutdown)
    {
        return;
    }

    if (isRenderableQueued(key))
    {
        return;
    }

    m_asyncRenderQueue.push_back(key);

    if (!m_asyncWorkerRunning.load())
    {
        m_asyncWorkerRunning = true;
        if (!m_asyncRenderThread.joinable())
        {
            m_asyncRenderThread = std::thread(&MuPdfDocument::asyncRenderWorker, this);
        }
    }

    lock.unlock();
    m_asyncRenderCv.notify_one();
}
int MuPdfDocument::getPageWidthNative(int pageNumber)
{
    if (!m_ctx || !m_doc)
        return 0;

    std::lock_guard<std::mutex> renderLock(m_renderMutex);
    try
    {
        ensureDisplayList(pageNumber);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 0;
    }

    std::lock_guard<std::mutex> dataLock(m_pageDataMutex);
    if (pageNumber < 0 || pageNumber >= static_cast<int>(m_pageDisplayData.size()))
        return 0;

    const auto& entry = m_pageDisplayData[pageNumber];
    if (!entry.displayList)
        return 0;

    return std::max(1, static_cast<int>(std::round(entry.bounds.x1 - entry.bounds.x0)));
}

int MuPdfDocument::getPageHeightNative(int pageNumber)
{
    if (!m_ctx || !m_doc)
        return 0;

    std::lock_guard<std::mutex> renderLock(m_renderMutex);
    try
    {
        ensureDisplayList(pageNumber);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 0;
    }

    std::lock_guard<std::mutex> dataLock(m_pageDataMutex);
    if (pageNumber < 0 || pageNumber >= static_cast<int>(m_pageDisplayData.size()))
        return 0;

    const auto& entry = m_pageDisplayData[pageNumber];
    if (!entry.displayList)
        return 0;

    return std::max(1, static_cast<int>(std::round(entry.bounds.y1 - entry.bounds.y0)));
}

std::pair<int, int> MuPdfDocument::getPageDimensionsEffective(int pageNumber, int zoom)
{
    auto key = std::make_pair(pageNumber, zoom);

    {
        std::lock_guard<std::mutex> dataLock(m_pageDataMutex);
        auto it = m_dimensionCache.find(key);
        if (it != m_dimensionCache.end())
        {
            return it->second;
        }
    }

    if (!m_ctx || !m_doc)
        return {0, 0};

    std::lock_guard<std::mutex> renderLock(m_renderMutex);

    try
    {
        ensureDisplayList(pageNumber);
        PageScaleInfo info = computePageScaleInfoLocked(pageNumber, zoom);
        return {info.width, info.height};
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return {0, 0};
    }
}

int MuPdfDocument::getPageWidthEffective(int pageNumber, int zoom)
{
    auto dims = getPageDimensionsEffective(pageNumber, zoom);
    return dims.first;
}

int MuPdfDocument::getPageHeightEffective(int pageNumber, int zoom)
{
    auto dims = getPageDimensionsEffective(pageNumber, zoom);
    return dims.second;
}

int MuPdfDocument::getPageCount() const
{
    return m_pageCount;
}

void MuPdfDocument::setMaxRenderSize(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (width == m_maxWidth && height == m_maxHeight)
    {
        return;
    }

    m_maxWidth = width;
    m_maxHeight = height;
    {
        std::lock_guard<std::mutex> dataLock(m_pageDataMutex);
        m_dimensionCache.clear();
    }
}

void MuPdfDocument::close()
{
    cancelPrerendering();
    joinAsyncRenderThread();

    m_doc.reset();
    m_ctx.reset();
    m_prerenderDoc.reset();
    m_prerenderCtx.reset();

    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache.clear();
        m_argbCache.clear();
    }

    {
        std::lock_guard<std::mutex> dataLock(m_pageDataMutex);
        m_dimensionCache.clear();
    }

    m_pageCount = 0;
    resetDisplayCache();
}

void MuPdfDocument::clearCache()
{
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache.clear();
        m_argbCache.clear();
    }
    {
        std::lock_guard<std::mutex> dataLock(m_pageDataMutex);
        m_dimensionCache.clear();
    }
}

void MuPdfDocument::cancelPrerendering()
{
    m_prerenderGeneration.fetch_add(1, std::memory_order_relaxed);

    if (m_prerenderThread.joinable())
    {
        m_prerenderActive = false;
        m_prerenderThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_asyncRenderMutex);
        m_asyncRenderQueue.clear();
    }
}

void MuPdfDocument::prerenderPage(int pageNumber, int scale)
{
    uint64_t generation = m_prerenderGeneration.load(std::memory_order_relaxed);
    prerenderPageInternal(pageNumber, scale, generation);
}

void MuPdfDocument::prerenderAdjacentPages(int currentPage, int scale)
{
    uint64_t generation = m_prerenderGeneration.load(std::memory_order_relaxed);
    prerenderAdjacentPagesInternal(currentPage, scale, generation);
}

void MuPdfDocument::prerenderAdjacentPagesAsync(int currentPage, int scale)
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPrerenderTime).count();
    if (elapsed < PRERENDER_COOLDOWN_MS)
    {
        return;
    }

    if (m_prerenderActive)
    {
        return;
    }

    if (m_prerenderThread.joinable())
    {
        m_prerenderActive = false;
        m_prerenderThread.join();
    }

    m_lastPrerenderTime = now;
    m_prerenderActive = true;

    uint64_t generation = m_prerenderGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

    m_prerenderThread = std::thread([this, currentPage, scale, generation]()
                                    {
        try
        {
            prerenderAdjacentPagesInternal(currentPage, scale, generation);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Background prerendering failed: " << e.what() << std::endl;
        }

        m_prerenderActive = false; });
}

void MuPdfDocument::setUserCSSBeforeOpen(const std::string& css)
{
    // Store CSS to be applied when document is opened
    m_userCSS = css;
    std::cout << "CSS set for application before document opening: " << css << std::endl;
}

void MuPdfDocument::resetDisplayCache()
{
    std::lock_guard<std::mutex> lock(m_pageDataMutex);
    m_pageDisplayData.clear();
    if (m_pageCount > 0)
    {
        m_pageDisplayData.resize(static_cast<size_t>(m_pageCount));
    }
    m_dimensionCache.clear();
}

void MuPdfDocument::ensureDisplayList(int pageNumber)
{
    {
        std::lock_guard<std::mutex> dataLock(m_pageDataMutex);
        if (pageNumber >= 0 && pageNumber < static_cast<int>(m_pageDisplayData.size()) && m_pageDisplayData[pageNumber].displayList)
        {
            return;
        }
    }

    fz_context* ctx = m_ctx.get();
    fz_document* doc = m_doc.get();
    if (!ctx || !doc)
    {
        throw std::runtime_error("MuPDF context or document is null while building display list");
    }

    fz_page* page = nullptr;
    fz_display_list* list = nullptr;
    fz_device* device = nullptr;
    fz_rect bounds{};

    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, pageNumber);
        if (!page)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page %d", pageNumber);
        }

        bounds = fz_bound_page(ctx, page);

        list = fz_new_display_list(ctx, bounds);
        if (!list)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to allocate display list for page %d", pageNumber);
        }

        device = fz_new_list_device(ctx, list);
        if (!device)
        {
            fz_drop_display_list(ctx, list);
            list = nullptr;
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to create list device for page %d", pageNumber);
        }

        fz_run_page(ctx, page, device, fz_identity, nullptr);
        fz_close_device(ctx, device);
        fz_drop_device(ctx, device);
        device = nullptr;

        fz_drop_page(ctx, page);
        page = nullptr;
    }
    fz_catch(ctx)
    {
        if (device)
            fz_drop_device(ctx, device);
        if (list)
            fz_drop_display_list(ctx, list);
        if (page)
            fz_drop_page(ctx, page);

        const char* muErr = fz_caught_message(ctx);
        std::string message = "Failed to build display list for page " + std::to_string(pageNumber);
        if (muErr && strlen(muErr) > 0)
        {
            message += ": ";
            message += muErr;
        }
        throw std::runtime_error(message);
    }

    std::unique_ptr<fz_display_list, DisplayListDeleter> listPtr(list, DisplayListDeleter{ctx});

    {
        std::lock_guard<std::mutex> dataLock(m_pageDataMutex);
        if (pageNumber >= static_cast<int>(m_pageDisplayData.size()))
        {
            m_pageDisplayData.resize(static_cast<size_t>(m_pageCount));
        }

        auto& entry = m_pageDisplayData[pageNumber];
        if (!entry.displayList)
        {
            entry.displayList = std::move(listPtr);
            entry.bounds = bounds;
        }
        // If another thread already populated the entry, listPtr will fall out of scope and release resources.
    }
}

MuPdfDocument::PageScaleInfo MuPdfDocument::computePageScaleInfoLocked(int pageNumber, int zoom)
{
    PageScaleInfo info{};
    info.baseScale = std::max(zoom, 1) / 100.0f;

    std::pair<int, int> dimensionKey = std::make_pair(pageNumber, zoom);

    {
        std::lock_guard<std::mutex> dataLock(m_pageDataMutex);
        if (pageNumber >= 0 && pageNumber < static_cast<int>(m_pageDisplayData.size()))
        {
            info.displayList = m_pageDisplayData[pageNumber].displayList.get();
            info.bounds = m_pageDisplayData[pageNumber].bounds;
        }
    }

    if (!info.displayList)
    {
        throw std::runtime_error("Display list missing for page " + std::to_string(pageNumber));
    }

    int nativeWidth = static_cast<int>(std::round(info.bounds.x1 - info.bounds.x0));
    int nativeHeight = static_cast<int>(std::round(info.bounds.y1 - info.bounds.y0));
    nativeWidth = std::max(nativeWidth, 1);
    nativeHeight = std::max(nativeHeight, 1);

    int scaledWidth = static_cast<int>(std::round(nativeWidth * info.baseScale));
    int scaledHeight = static_cast<int>(std::round(nativeHeight * info.baseScale));

    info.downsampleScale = 1.0f;
    if (scaledWidth > m_maxWidth || scaledHeight > m_maxHeight)
    {
        float scaleX = static_cast<float>(m_maxWidth) / std::max(scaledWidth, 1);
        float scaleY = static_cast<float>(m_maxHeight) / std::max(scaledHeight, 1);
        info.downsampleScale = std::min(scaleX, scaleY);

        if (info.baseScale > 1.5f)
        {
            float extraDetail = std::min(info.baseScale - 1.5f, 0.5f) * 0.1f;
            info.downsampleScale = std::min(info.downsampleScale * (1.0f + extraDetail), 1.0f);
        }
    }

    float finalScale = info.baseScale * info.downsampleScale;
    info.transform = fz_scale(finalScale, finalScale);

    fz_rect transformed = info.bounds;
    transformed = fz_transform_rect(transformed, info.transform);

    info.bbox = fz_round_rect(transformed);
    info.width = std::max(1, info.bbox.x1 - info.bbox.x0);
    info.height = std::max(1, info.bbox.y1 - info.bbox.y0);

    {
        std::lock_guard<std::mutex> dataLock(m_pageDataMutex);
        m_dimensionCache[dimensionKey] = {info.width, info.height};
    }

    return info;
}

void MuPdfDocument::joinAsyncRenderThread()
{
    {
        std::lock_guard<std::mutex> lock(m_asyncRenderMutex);
        m_asyncShutdown = true;
        m_asyncRenderQueue.clear();
    }
    m_asyncRenderCv.notify_all();

    if (m_asyncRenderThread.joinable())
    {
        m_asyncRenderThread.join();
    }

    m_asyncWorkerRunning = false;
}

bool MuPdfDocument::isRenderableQueued(const std::pair<int, int>& key)
{
    for (const auto& pending : m_asyncRenderQueue)
    {
        if (pending == key)
        {
            return true;
        }
    }
    return false;
}

void MuPdfDocument::asyncRenderWorker()
{
    while (true)
    {
        std::pair<int, int> task{-1, 0};

        {
            std::unique_lock<std::mutex> lock(m_asyncRenderMutex);
            m_asyncRenderCv.wait(lock, [this]()
                                 { return m_asyncShutdown || !m_asyncRenderQueue.empty(); });

            if ((m_asyncShutdown && m_asyncRenderQueue.empty()))
            {
                m_asyncWorkerRunning = false;
                return;
            }

            task = m_asyncRenderQueue.front();
            m_asyncRenderQueue.pop_front();
        }

        if (task.first < 0)
        {
            continue;
        }

        auto key = std::make_pair(task.first, task.second);

        {
            std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
            if (m_argbCache.find(key) != m_argbCache.end())
            {
                continue; // Already cached, skip expensive render
            }
        }

        try
        {
            int tmpW = 0;
            int tmpH = 0;
            renderPageARGB(task.first, tmpW, tmpH, task.second);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Async render failed for page " << task.first << " scale " << task.second << ": " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Async render encountered unknown error for page " << task.first << std::endl;
        }
    }
}

bool MuPdfDocument::isPrerenderRequestStale(uint64_t generationToken) const
{
    return generationToken != m_prerenderGeneration.load(std::memory_order_relaxed);
}

void MuPdfDocument::prerenderPageInternal(int pageNumber, int scale, uint64_t generationToken)
{
    if (isPrerenderRequestStale(generationToken))
    {
        return;
    }

    // Original body from prerenderPage with added stale checks
    // Validate page number
    if (pageNumber < 0 || pageNumber >= m_pageCount)
    {
        return;
    }

    auto key = std::make_pair(pageNumber, scale);
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_cache.find(key) != m_cache.end() || m_argbCache.find(key) != m_argbCache.end())
        {
            return; // Already cached
        }
    }

    std::lock_guard<std::mutex> prerenderLock(m_prerenderMutex);

    if (!m_prerenderCtx || !m_prerenderDoc || isPrerenderRequestStale(generationToken))
    {
        return;
    }

    fz_context* ctx = m_prerenderCtx.get();
    fz_document* doc = m_prerenderDoc.get();

    try
    {
        float baseScale = scale / 100.0f;
        float downsampleScale = 1.0f;
        fz_var(downsampleScale);

        fz_page* tempPage = nullptr;
        fz_var(tempPage);
        bool prerenderBoundsError = false;
        fz_var(prerenderBoundsError);

        fz_try(ctx)
        {
            tempPage = fz_load_page(ctx, doc, pageNumber);
            if (!tempPage)
            {
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page for prerender");
            }

            if (isPrerenderRequestStale(generationToken))
            {
                fz_drop_page(ctx, tempPage);
                tempPage = nullptr;
                fz_throw(ctx, FZ_ERROR_GENERIC, "Prerender request stale");
            }

            fz_rect bounds = fz_bound_page(ctx, tempPage);
            int nativeWidth = static_cast<int>((bounds.x1 - bounds.x0) * baseScale);
            int nativeHeight = static_cast<int>((bounds.y1 - bounds.y0) * baseScale);

            if (nativeWidth > m_maxWidth || nativeHeight > m_maxHeight)
            {
                float scaleX = static_cast<float>(m_maxWidth) / std::max(nativeWidth, 1);
                float scaleY = static_cast<float>(m_maxHeight) / std::max(nativeHeight, 1);
                downsampleScale = std::min(scaleX, scaleY);

                if (baseScale > 1.5f)
                {
                    float extraDetail = std::min(baseScale - 1.5f, 0.5f) * 0.1f;
                    downsampleScale = std::min(downsampleScale * (1.0f + extraDetail), 1.0f);
                }
            }
            fz_drop_page(ctx, tempPage);
            tempPage = nullptr;
        }
        fz_catch(ctx)
        {
            if (tempPage)
                fz_drop_page(ctx, tempPage);
            prerenderBoundsError = true;
        }

        if (prerenderBoundsError || isPrerenderRequestStale(generationToken))
        {
            return;
        }

        fz_matrix transform = fz_scale(baseScale * downsampleScale, baseScale * downsampleScale);
        fz_pixmap* pix = nullptr;
        fz_var(pix);
        std::vector<unsigned char> buffer;

        bool prerenderError = false;
        std::string prerenderErrorMsg;
        const char* prerenderMuPdfMsg = nullptr;
        fz_var(prerenderError);
        fz_var(prerenderMuPdfMsg);

        fz_try(ctx)
        {
            fz_page* page = fz_load_page(ctx, doc, pageNumber);
            fz_var(page);
            if (!page)
            {
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page %d for prerendering", pageNumber);
            }

            if (isPrerenderRequestStale(generationToken))
            {
                fz_drop_page(ctx, page);
                fz_throw(ctx, FZ_ERROR_GENERIC, "Prerender request stale");
            }

            pix = fz_new_pixmap_from_page(ctx, page, transform, fz_device_rgb(ctx), 0);
            if (!pix)
            {
                fz_drop_page(ctx, page);
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to create pixmap for prerender page %d", pageNumber);
            }

            fz_drop_page(ctx, page);

            int w = fz_pixmap_width(ctx, pix);
            int h = fz_pixmap_height(ctx, pix);

            size_t dataSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
            buffer.resize(dataSize);
            memcpy(buffer.data(), fz_pixmap_samples(ctx, pix), dataSize);

            fz_drop_pixmap(ctx, pix);
            pix = nullptr;

            if (!isPrerenderRequestStale(generationToken))
            {
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                m_cache[key] = std::make_tuple(buffer, w, h);
            }
        }
        fz_catch(ctx)
        {
            if (pix)
                fz_drop_pixmap(ctx, pix);
            prerenderError = true;
            prerenderErrorMsg = "Error prerendering page " + std::to_string(pageNumber);
            prerenderMuPdfMsg = fz_caught_message(ctx);
        }

        if (prerenderError)
        {
            bool isStale = prerenderMuPdfMsg && strstr(prerenderMuPdfMsg, "Prerender request stale");
            if (!isStale)
            {
                if (prerenderMuPdfMsg && strlen(prerenderMuPdfMsg) > 0)
                {
                    prerenderErrorMsg += ": " + std::string(prerenderMuPdfMsg);
                }
                std::cerr << "MuPdfDocument: " << prerenderErrorMsg << std::endl;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception during prerender of page " << pageNumber << ": " << e.what() << std::endl;
    }
}

void MuPdfDocument::prerenderAdjacentPagesInternal(int currentPage, int scale, uint64_t generationToken)
{
    if (isPrerenderRequestStale(generationToken))
    {
        return;
    }

    if (currentPage + 1 < m_pageCount)
    {
        prerenderPageInternal(currentPage + 1, scale, generationToken);
    }

    if (isPrerenderRequestStale(generationToken))
    {
        return;
    }

    if (currentPage - 1 >= 0)
    {
        prerenderPageInternal(currentPage - 1, scale, generationToken);
    }

    if (isPrerenderRequestStale(generationToken))
    {
        return;
    }

    if (currentPage + 2 < m_pageCount)
    {
        prerenderPageInternal(currentPage + 2, scale, generationToken);
    }
}
