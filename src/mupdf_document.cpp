#include "mupdf_document.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

MuPdfDocument::MuPdfDocument()
    : Document()
{
    // Initialize MuPDF context with limited store size to prevent corruption
    m_ctx = std::unique_ptr<fz_context, ContextDeleter>(fz_new_context(nullptr, nullptr, 256 << 20)); // 256MB
}

MuPdfDocument::~MuPdfDocument()
{
    std::cout << "MuPdfDocument::~MuPdfDocument(): Destructor called" << std::endl;
    std::cout.flush();
    
    // Stop background prerendering before destroying the object
    if (m_prerenderThread.joinable())
    {
        std::cout << "MuPdfDocument::~MuPdfDocument(): Stopping prerender thread..." << std::endl;
        std::cout.flush();
        m_prerenderActive = false;
        m_prerenderThread.join();
        std::cout << "MuPdfDocument::~MuPdfDocument(): Prerender thread stopped" << std::endl;
        std::cout.flush();
    }

    std::cout << "MuPdfDocument::~MuPdfDocument(): Clearing caches..." << std::endl;
    std::cout.flush();
    
    // Cleanup is handled by unique_ptr deleters
    // Clear cache
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_cache.clear();
    m_argbCache.clear();
    
    std::cout << "MuPdfDocument::~MuPdfDocument(): Destructor complete" << std::endl;
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
    // Protect all MuPDF operations with mutex to prevent race conditions
    std::lock_guard<std::mutex> lock(m_renderMutex);

    if (!m_ctx || !m_doc)
    {
        throw std::runtime_error("Document not open");
    }

    // Validate page number
    if (pageNumber < 0 || pageNumber >= m_pageCount)
    {
        throw std::runtime_error("Invalid page number: " + std::to_string(pageNumber));
    }

    fz_context* ctx = m_ctx.get();
    fz_document* doc = m_doc.get();

    // Additional safety check
    if (!ctx || !doc)
    {
        throw std::runtime_error("MuPDF context or document is null");
    }

    // Calculate transform including downsampling
    float baseScale = zoom / 100.0f;
    float downsampleScale = 1.0f;
    fz_var(downsampleScale);

    // Pre-calculate if we need downsampling to avoid fz_scale_pixmap
    fz_page* tempPage = nullptr;
    fz_var(tempPage);
    bool boundsError = false;
    std::string boundsErrorMsg;

    fz_try(ctx)
    {
        // Check if page number is valid before attempting to load
        int totalPages = fz_count_pages(ctx, doc);
        if (pageNumber >= totalPages)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Page number out of range: %d >= %d", pageNumber, totalPages);
        }

        tempPage = fz_load_page(ctx, doc, pageNumber);
        if (!tempPage)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page %d", pageNumber);
        }

        fz_rect bounds = fz_bound_page(ctx, (fz_page*) tempPage);
        int nativeWidth = static_cast<int>((bounds.x1 - bounds.x0) * baseScale);
        int nativeHeight = static_cast<int>((bounds.y1 - bounds.y0) * baseScale);

        // Only downsample if the zoomed image is larger than max size
        // This allows zooming in for detail up to the max render size
        if (nativeWidth > m_maxWidth || nativeHeight > m_maxHeight)
        {
            // Simple and fast downsampling: just fit to max dimensions
            float scaleX = static_cast<float>(m_maxWidth) / nativeWidth;
            float scaleY = static_cast<float>(m_maxHeight) / nativeHeight;
            downsampleScale = std::min(scaleX, scaleY);

            // For zoom levels above 150%, allow slightly more detail to prevent quality cliff
            // But keep it simple to avoid performance issues
            if (baseScale > 1.5f)
            {
                // Allow up to 10% more detail for high zoom levels, but cap it
                float extraDetail = std::min(baseScale - 1.5f, 0.5f) * 0.1f; // Max 5% extra
                downsampleScale = std::min(downsampleScale * (1.0f + extraDetail), 1.0f);
            }
        }
        fz_drop_page(ctx, (fz_page*) tempPage);
        tempPage = nullptr;
    }
    fz_catch(ctx)
    {
        if (tempPage)
            fz_drop_page(ctx, (fz_page*) tempPage);
        boundsError = true;
        boundsErrorMsg = "Error calculating page bounds for page " + std::to_string(pageNumber);
    }

    if (boundsError)
    {
        std::cerr << "MuPdfDocument: " << boundsErrorMsg << std::endl;
        throw std::runtime_error(boundsErrorMsg);
    }

    // Check cache using exact zoom level for cache key
    auto key = std::make_pair(pageNumber, zoom); // zoom is already an integer percentage

    // Reduce debug output frequency for better performance at high zoom levels
    static int debugCounter = 0;
    bool shouldDebug = (++debugCounter % 10 == 0) || zoom < 150; // Debug every 10th call or for low zoom

    if (shouldDebug)
    {
        std::cout << "Render page " << pageNumber << ": zoom=" << zoom
                  << "%, baseScale=" << baseScale
                  << ", downsampleScale=" << downsampleScale
                  << ", exactZoom=" << zoom << "%" << std::endl;
    }

    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_cache.find(key);
        if (it != m_cache.end())
        {
            if (shouldDebug)
            {
                std::cout << "Cache HIT for page " << pageNumber << " at exact zoom " << zoom << "%" << std::endl;
            }
            auto& [buffer, cachedWidth, cachedHeight] = it->second;
            width = cachedWidth;
            height = cachedHeight;
            return buffer;
        }
        if (shouldDebug)
        {
            std::cout << "Cache MISS for page " << pageNumber << " at exact zoom " << zoom << "%" << std::endl;
        }
    }

    fz_matrix transform = fz_scale(baseScale * downsampleScale, baseScale * downsampleScale);
    fz_pixmap* pix = nullptr;
    fz_var(pix);
    std::vector<unsigned char> buffer;

    bool renderError = false;
    std::string renderErrorMsg;
    const char* renderMuPdfMsg = nullptr;
    fz_var(renderError);
    fz_var(renderMuPdfMsg);

    fz_try(ctx)
    {
        fz_page* page = fz_load_page(ctx, doc, pageNumber);
        fz_var(page);
        if (!page)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page %d for rendering", pageNumber);
        }

        pix = fz_new_pixmap_from_page(ctx, (fz_page*) page, transform, fz_device_rgb(ctx), 0);
        if (!pix)
        {
            fz_drop_page(ctx, (fz_page*) page);
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to create pixmap for page %d", pageNumber);
        }

        fz_drop_page(ctx, (fz_page*) page);

        width = fz_pixmap_width(ctx, (fz_pixmap*) pix);
        height = fz_pixmap_height(ctx, (fz_pixmap*) pix);

        // No post-processing scaling needed - downsampling applied in transform

        size_t dataSize = width * height * 3;
        buffer.resize(dataSize);
        memcpy(buffer.data(), fz_pixmap_samples(ctx, (fz_pixmap*) pix), dataSize);

        fz_drop_pixmap(ctx, (fz_pixmap*) pix);
        pix = nullptr;
    }
    fz_catch(ctx)
    {
        if (pix)
            fz_drop_pixmap(ctx, (fz_pixmap*) pix);
        renderError = true;
        renderErrorMsg = "Error rendering page " + std::to_string(pageNumber);
        renderMuPdfMsg = fz_caught_message(ctx);
    }

    if (renderError)
    {
        if (renderMuPdfMsg && strlen(renderMuPdfMsg) > 0)
        {
            renderErrorMsg += ": " + std::string(renderMuPdfMsg);

            if (strstr(renderMuPdfMsg, "cycle in page tree"))
            {
                renderErrorMsg += " (PDF has circular page tree references - document may be corrupted)";
            }
            else if (strstr(renderMuPdfMsg, "cannot load object"))
            {
                renderErrorMsg += " (PDF object corruption detected)";
            }
        }

        std::cerr << "MuPdfDocument: " << renderErrorMsg << std::endl;
        throw std::runtime_error(renderErrorMsg);
    }

    // Cache the result
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);

        // For high zoom levels, limit cache size to prevent memory issues
        if (zoom > 200 && m_cache.size() > 10)
        {
            // Keep only the most recent entries for high zoom levels
            auto it = m_cache.begin();
            std::advance(it, m_cache.size() - 5); // Keep last 5 entries
            m_cache.erase(m_cache.begin(), it);
        }
        else if (m_cache.size() > 20)
        {
            // General cache size limit
            auto it = m_cache.begin();
            std::advance(it, m_cache.size() - 15); // Keep last 15 entries
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
int MuPdfDocument::getPageWidthNative(int pageNumber)
{
    if (!m_ctx || !m_doc)
        return 0;

    fz_context* ctx = m_ctx.get();
    fz_document* doc = m_doc.get();
    int width = 0;
    fz_var(width);

    fz_try(ctx)
    {
        fz_page* page = fz_load_page(ctx, doc, pageNumber);
        fz_var(page);
        fz_rect bounds = fz_bound_page(ctx, (fz_page*) page);
        width = static_cast<int>(bounds.x1 - bounds.x0);
        fz_drop_page(ctx, (fz_page*) page);
    }
    fz_catch(ctx)
    {
        width = 0;
    }

    return width; // implicit cast from int to int
}

int MuPdfDocument::getPageHeightNative(int pageNumber)
{
    if (!m_ctx || !m_doc)
        return 0;

    fz_context* ctx = m_ctx.get();
    fz_document* doc = m_doc.get();
    int height = 0;
    fz_var(height);

    fz_try(ctx)
    {
        fz_page* page = fz_load_page(ctx, doc, pageNumber);
        fz_var(page);
        fz_rect bounds = fz_bound_page(ctx, (fz_page*) page);
        height = static_cast<int>(bounds.y1 - bounds.y0);
        fz_drop_page(ctx, (fz_page*) page);
    }
    fz_catch(ctx)
    {
        height = 0;
    }

    return height;
}

int MuPdfDocument::getPageWidthEffective(int pageNumber, int zoom)
{
    if (!m_ctx || !m_doc)
        return 0;

    fz_context* ctx = m_ctx.get();
    fz_document* doc = m_doc.get();
    int width = 0;
    fz_var(width);

    fz_try(ctx)
    {
        fz_page* page = fz_load_page(ctx, doc, pageNumber);
        fz_var(page);
        fz_rect bounds = fz_bound_page(ctx, (fz_page*) page);
        int nativeWidth = static_cast<int>(bounds.x1 - bounds.x0);
        fz_drop_page(ctx, (fz_page*) page);

        // Apply zoom
        float baseScale = zoom / 100.0f;
        int scaledWidth = static_cast<int>(nativeWidth * baseScale);

        // Apply downsampling logic matching renderPage
        if (scaledWidth > m_maxWidth)
        {
            float downsampleScale = static_cast<float>(m_maxWidth) / scaledWidth;

            // For zoom levels above 150%, allow slightly more detail to prevent quality cliff
            // But keep it simple to avoid performance issues
            if (baseScale > 1.5f)
            {
                // Allow up to 10% more detail for high zoom levels, but cap it
                float extraDetail = std::min(baseScale - 1.5f, 0.5f) * 0.1f; // Max 5% extra
                downsampleScale = std::min(downsampleScale * (1.0f + extraDetail), 1.0f);
            }

            width = static_cast<int>(scaledWidth * downsampleScale);
        }
        else
        {
            width = scaledWidth;
        }
    }
    fz_catch(ctx)
    {
        width = 0;
    }

    return width;
}

int MuPdfDocument::getPageHeightEffective(int pageNumber, int zoom)
{
    if (!m_ctx || !m_doc)
        return 0;

    fz_context* ctx = m_ctx.get();
    fz_document* doc = m_doc.get();
    int height = 0;
    fz_var(height);

    fz_try(ctx)
    {
        fz_page* page = fz_load_page(ctx, doc, pageNumber);
        fz_var(page);
        fz_rect bounds = fz_bound_page(ctx, (fz_page*) page);
        int nativeWidth = static_cast<int>(bounds.x1 - bounds.x0);
        int nativeHeight = static_cast<int>(bounds.y1 - bounds.y0);
        fz_drop_page(ctx, (fz_page*) page);

        // Apply zoom
        float baseScale = zoom / 100.0f;
        int scaledWidth = static_cast<int>(nativeWidth * baseScale);
        int scaledHeight = static_cast<int>(nativeHeight * baseScale);

        // Apply downsampling logic matching renderPage (maintain aspect ratio)
        if (scaledWidth > m_maxWidth || scaledHeight > m_maxHeight)
        {
            float scaleX = static_cast<float>(m_maxWidth) / scaledWidth;
            float scaleY = static_cast<float>(m_maxHeight) / scaledHeight;
            float downsampleScale = std::min(scaleX, scaleY);

            // For zoom levels above 150%, allow slightly more detail to prevent quality cliff
            // But keep it simple to avoid performance issues
            if (baseScale > 1.5f)
            {
                // Allow up to 10% more detail for high zoom levels, but cap it
                float extraDetail = std::min(baseScale - 1.5f, 0.5f) * 0.1f; // Max 5% extra
                downsampleScale = std::min(downsampleScale * (1.0f + extraDetail), 1.0f);
            }

            height = static_cast<int>(scaledHeight * downsampleScale);
        }
        else
        {
            height = scaledHeight;
        }
    }
    fz_catch(ctx)
    {
        height = 0;
    }

    return height;
}

int MuPdfDocument::getPageCount() const
{
    return m_pageCount;
}

void MuPdfDocument::setMaxRenderSize(int width, int height)
{
    m_maxWidth = width;
    m_maxHeight = height;
}

void MuPdfDocument::close()
{
    m_doc.reset();
    m_ctx.reset();
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache.clear();
    }
}

void MuPdfDocument::clearCache()
{
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_cache.clear();
    m_argbCache.clear();
}

void MuPdfDocument::cancelPrerendering()
{
    // Stop any existing background prerendering
    if (m_prerenderThread.joinable())
    {
        m_prerenderActive = false;
        m_prerenderThread.join();
    }
}

void MuPdfDocument::prerenderPage(int pageNumber, int scale)
{
    // Validate page number
    if (pageNumber < 0 || pageNumber >= m_pageCount)
    {
        return;
    }

    // Check if already cached
    auto key = std::make_pair(pageNumber, scale);
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_cache.find(key) != m_cache.end())
        {
            return; // Already cached
        }
    }

    // Use separate context and mutex for prerendering to avoid race conditions
    std::lock_guard<std::mutex> prerenderLock(m_prerenderMutex);

    if (!m_prerenderCtx || !m_prerenderDoc)
    {
        std::cerr << "Prerender context not available for page " << pageNumber << std::endl;
        return;
    }

    fz_context* ctx = m_prerenderCtx.get();
    fz_document* doc = m_prerenderDoc.get();

    try
    {
        // Calculate transform
        float baseScale = scale / 100.0f;
        float downsampleScale = 1.0f;
        fz_var(downsampleScale);

        // Apply downsampling logic matching renderPage
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

            fz_rect bounds = fz_bound_page(ctx, tempPage);
            int nativeWidth = static_cast<int>((bounds.x1 - bounds.x0) * baseScale);
            int nativeHeight = static_cast<int>((bounds.y1 - bounds.y0) * baseScale);

            // Only downsample if the zoomed image is larger than max size
            // This allows zooming in for detail up to the max render size
            if (nativeWidth > m_maxWidth || nativeHeight > m_maxHeight)
            {
                // Simple and fast downsampling: just fit to max dimensions
                float scaleX = static_cast<float>(m_maxWidth) / nativeWidth;
                float scaleY = static_cast<float>(m_maxHeight) / nativeHeight;
                downsampleScale = std::min(scaleX, scaleY);

                // For zoom levels above 150%, allow slightly more detail to prevent quality cliff
                // But keep it simple to avoid performance issues
                if (baseScale > 1.5f)
                {
                    // Allow up to 10% more detail for high zoom levels, but cap it
                    float extraDetail = std::min(baseScale - 1.5f, 0.5f) * 0.1f; // Max 5% extra
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

        if (prerenderBoundsError)
        {
            std::cerr << "Error calculating page bounds for prerender page " << pageNumber << std::endl;
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

            pix = fz_new_pixmap_from_page(ctx, page, transform, fz_device_rgb(ctx), 0);
            if (!pix)
            {
                fz_drop_page(ctx, page);
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to create pixmap for prerender page %d", pageNumber);
            }

            fz_drop_page(ctx, page);

            int w = fz_pixmap_width(ctx, pix);
            int h = fz_pixmap_height(ctx, pix);

            size_t dataSize = w * h * 3;
            buffer.resize(dataSize);
            memcpy(buffer.data(), fz_pixmap_samples(ctx, pix), dataSize);

            fz_drop_pixmap(ctx, pix);
            pix = nullptr;

            // Cache the result
            {
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                m_cache[key] = std::make_tuple(buffer, w, h);
            }

            std::cout << "Prerendered page " << pageNumber << " at scale " << scale << "%" << std::endl;
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
            if (prerenderMuPdfMsg && strlen(prerenderMuPdfMsg) > 0)
            {
                prerenderErrorMsg += ": " + std::string(prerenderMuPdfMsg);
            }
            std::cerr << "MuPdfDocument: " << prerenderErrorMsg << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception during prerender of page " << pageNumber << ": " << e.what() << std::endl;
    }
}

void MuPdfDocument::prerenderAdjacentPages(int currentPage, int scale)
{
    // Prerender next page first (most common navigation)
    if (currentPage + 1 < m_pageCount)
    {
        prerenderPage(currentPage + 1, scale);
    }

    // Prerender previous page
    if (currentPage - 1 >= 0)
    {
        prerenderPage(currentPage - 1, scale);
    }

    // For better user experience, also prerender the page after next
    if (currentPage + 2 < m_pageCount)
    {
        prerenderPage(currentPage + 2, scale);
    }
}

void MuPdfDocument::prerenderAdjacentPagesAsync(int currentPage, int scale)
{
    // Check cooldown to prevent excessive prerendering
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPrerenderTime).count();
    if (elapsed < PRERENDER_COOLDOWN_MS)
    {
        return; // Too soon, skip prerendering
    }

    // Don't start new prerendering if already active
    if (m_prerenderActive)
    {
        return;
    }

    // Stop any existing background prerendering (shouldn't be necessary but safe)
    if (m_prerenderThread.joinable())
    {
        m_prerenderActive = false;
        m_prerenderThread.join();
    }

    // Update last prerender time
    m_lastPrerenderTime = now;

    // Start new background prerendering
    m_prerenderActive = true;
    m_prerenderThread = std::thread([this, currentPage, scale]()
                                    {
        try {
            // Prerender in order of user's likely navigation

            // 1. Next page (most likely)
            if (m_prerenderActive && currentPage + 1 < m_pageCount) {
                prerenderPage(currentPage + 1, scale);
            }

            // 2. Previous page (less likely but still common)
            if (m_prerenderActive && currentPage - 1 >= 0) {
                prerenderPage(currentPage - 1, scale);
            }

            // 3. Page after next (for reading ahead)
            if (m_prerenderActive && currentPage + 2 < m_pageCount) {
                prerenderPage(currentPage + 2, scale);
            }

        } catch (const std::exception& e) {
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
