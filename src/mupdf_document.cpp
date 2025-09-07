#include "mupdf_document.h"

#include <iostream>
#include <cstring>
#include <stdexcept>
#include <algorithm>

MuPdfDocument::MuPdfDocument()
    : Document()
{
    // Initialize MuPDF context with limited store size to prevent corruption
    m_ctx = std::unique_ptr<fz_context, ContextDeleter>(fz_new_context(nullptr, nullptr, 256 << 20)); // 256MB
}

MuPdfDocument::~MuPdfDocument()
{
    // Stop background prerendering before destroying the object
    if (m_prerenderThread.joinable()) {
        m_prerenderActive = false;
        m_prerenderThread.join();
    }
    
    // Cleanup is handled by unique_ptr deleters
    // Clear cache
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_cache.clear();
}

bool MuPdfDocument::open(const std::string &filePath)
{
    close();

    fz_context *ctx = fz_new_context(nullptr, nullptr, 256 << 20); // 256MB
    if (!ctx)
    {
        std::cerr << "Cannot create MuPDF context\n";
        return false;
    }

    m_ctx.reset(ctx);
    fz_register_document_handlers(ctx);

    // Create separate context for prerendering to avoid race conditions
    fz_context *prerenderCtx = fz_new_context(nullptr, nullptr, 256 << 20); // 256MB
    if (!prerenderCtx)
    {
        std::cerr << "Cannot create prerender MuPDF context\n";
        return false;
    }
    m_prerenderCtx.reset(prerenderCtx);
    fz_register_document_handlers(prerenderCtx);

    fz_document *doc = nullptr;
    fz_var(doc);
    
    fz_try(ctx)
    {
        doc = fz_open_document(ctx, filePath.c_str());
    }
    fz_catch(ctx)
    {
        const char* fzError = fz_caught_message(ctx);
        std::string errorMsg = "Failed to open document: " + filePath;
        if (fzError && strlen(fzError) > 0) {
            errorMsg += " (MuPDF error: " + std::string(fzError) + ")";
        }
        std::cerr << errorMsg << "\n";
        return false;
    }

    m_doc = std::unique_ptr<fz_document, DocumentDeleter>(doc, DocumentDeleter{ctx});
    
    // Also open document in prerender context
    fz_document *prerenderDocPtr = nullptr;
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

std::vector<unsigned char> MuPdfDocument::renderPage(int pageNumber, int &width, int &height, int zoom)
{
    // Protect all MuPDF operations with mutex to prevent race conditions
    std::lock_guard<std::mutex> lock(m_renderMutex);
    
    if (!m_ctx || !m_doc)
    {
        throw std::runtime_error("Document not open");
    }

    // Validate page number
    if (pageNumber < 0 || pageNumber >= m_pageCount) {
        throw std::runtime_error("Invalid page number: " + std::to_string(pageNumber));
    }

    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    
    // Additional safety check
    if (!ctx || !doc) {
        throw std::runtime_error("MuPDF context or document is null");
    }

    // Calculate transform including downsampling (main branch approach)
    float baseScale = zoom / 100.0f;
    float downsampleScale = 1.0f;
    fz_var(downsampleScale);
    
    // Pre-calculate if we need downsampling to avoid post-processing
    volatile fz_page *tempPage = nullptr; // Use volatile to prevent longjmp clobbering
    fz_var(tempPage);
    fz_try(ctx)
    {
        // Check if page number is valid before attempting to load
        int totalPages = fz_count_pages(ctx, doc);
        if (pageNumber >= totalPages) {
            throw std::runtime_error("Page number out of range: " + 
                std::to_string(pageNumber) + " >= " + std::to_string(totalPages));
        }
        
        tempPage = fz_load_page(ctx, doc, pageNumber);
        if (!tempPage) {
            throw std::runtime_error("Failed to load page " + std::to_string(pageNumber));
        }
        
        fz_rect bounds = fz_bound_page(ctx, (fz_page*)tempPage);
        int nativeWidth = static_cast<int>((bounds.x1 - bounds.x0) * baseScale);
        int nativeHeight = static_cast<int>((bounds.y1 - bounds.y0) * baseScale);
        
        // Only downsample if the zoomed image is significantly larger than max size
        // Allow some oversize rendering for better text quality
        const float oversizeTolerance = 1.5f; // Allow 50% oversize before downsampling
        if (nativeWidth > m_maxWidth * oversizeTolerance || nativeHeight > m_maxHeight * oversizeTolerance)
        {
            float scaleX = static_cast<float>(m_maxWidth) / nativeWidth;
            float scaleY = static_cast<float>(m_maxHeight) / nativeHeight;
            downsampleScale = std::min(scaleX, scaleY);
            
            // Gradual scaling for zoom levels - avoid cliff effects
            if (baseScale > 1.0f) {
                // Calculate how much detail we can afford based on zoom level
                // Higher zoom = more detail allowed, up to 3.5x window size (350% zoom)
                float zoomFactor = std::min(baseScale, 3.5f); // Cap at 3.5x
                float maxDetailScale = zoomFactor;
                float detailScaleX = static_cast<float>(m_maxWidth * maxDetailScale) / nativeWidth;
                float detailScaleY = static_cast<float>(m_maxHeight * maxDetailScale) / nativeHeight;
                float detailScale = std::min(detailScaleX, detailScaleY);
                
                // Use the more permissive scale, but ensure smooth transitions
                downsampleScale = std::max(downsampleScale, detailScale);
            }
        }
        fz_drop_page(ctx, (fz_page*)tempPage);
    }
    fz_catch(ctx)
    {
        if (tempPage) fz_drop_page(ctx, (fz_page*)tempPage);
        std::string errorMsg = "Error calculating page bounds for page " + std::to_string(pageNumber);
        std::cerr << "MuPdfDocument: " << errorMsg << std::endl;
        throw std::runtime_error(errorMsg);
    }

    // Check cache using exact zoom level for cache key
    auto key = std::make_pair(pageNumber, zoom); // zoom is already an integer percentage
    
    std::cout << "Render page " << pageNumber << ": zoom=" << zoom 
              << "%, baseScale=" << baseScale 
              << ", downsampleScale=" << downsampleScale 
              << ", exactZoom=" << zoom << "%" << std::endl;
    
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_cache.find(key);
        if (it != m_cache.end())
        {
            std::cout << "Cache HIT for page " << pageNumber << " at exact zoom " << zoom << "%" << std::endl;
            auto &[buffer, cachedWidth, cachedHeight] = it->second;
            width = cachedWidth;
            height = cachedHeight;
            return buffer;
        }
        std::cout << "Cache MISS for page " << pageNumber << " at exact zoom " << zoom << "%" << std::endl;
    }

    // Apply both base scale and downsample scale in the transform
    fz_matrix transform = fz_scale(baseScale * downsampleScale, baseScale * downsampleScale);
    volatile fz_pixmap *pix = nullptr; // Use volatile to prevent longjmp clobbering
    fz_var(pix);
    std::vector<unsigned char> buffer;
    
    fz_try(ctx)
    {
        volatile fz_page *page = fz_load_page(ctx, doc, pageNumber);
        if (!page) {
            throw std::runtime_error("Failed to load page " + std::to_string(pageNumber) + " for rendering");
        }

        pix = fz_new_pixmap_from_page(ctx, (fz_page*)page, transform, fz_device_rgb(ctx), 0);
        if (!pix) {
            fz_drop_page(ctx, (fz_page*)page);
            throw std::runtime_error("Failed to create pixmap for page " + std::to_string(pageNumber));
        }

        fz_drop_page(ctx, (fz_page*)page);

        width = fz_pixmap_width(ctx, (fz_pixmap*)pix);
        height = fz_pixmap_height(ctx, (fz_pixmap*)pix);

        // No post-processing scaling needed - downsampling applied in transform
        size_t dataSize = width * height * 3;
        buffer.resize(dataSize);
        memcpy(buffer.data(), fz_pixmap_samples(ctx, (fz_pixmap*)pix), dataSize);

        fz_drop_pixmap(ctx, (fz_pixmap*)pix);
        pix = nullptr;
    }
    fz_catch(ctx)
    {
        if (pix)
            fz_drop_pixmap(ctx, (fz_pixmap*)pix);
        
        // More detailed error reporting for different types of PDF corruption
        std::string errorMsg = "Error rendering page " + std::to_string(pageNumber);
        const char* fzError = fz_caught_message(ctx);
        if (fzError && strlen(fzError) > 0) {
            errorMsg += ": " + std::string(fzError);
            
            // Handle specific known PDF corruption issues
            if (strstr(fzError, "cycle in page tree")) {
                errorMsg += " (PDF has circular page tree references - document may be corrupted)";
            } else if (strstr(fzError, "cannot load object")) {
                errorMsg += " (PDF object corruption detected)";
            }
        }
        
        std::cerr << "MuPdfDocument: " << errorMsg << std::endl;
        throw std::runtime_error(errorMsg);
    }

    // Cache the result
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache[key] = std::make_tuple(buffer, width, height);
    }

    return buffer;
}
int MuPdfDocument::getPageWidthNative(int pageNumber)
{
    std::lock_guard<std::mutex> lock(m_renderMutex);
    
    if (!m_ctx || !m_doc)
        return 0;
    
    // Validate page number first
    if (pageNumber < 0 || pageNumber >= m_pageCount) {
        return 0;
    }

    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    
    // Additional safety check
    if (!ctx || !doc) {
        return 0;
    }
    
    int width = 0;
    fz_page *page = nullptr;
    
    fz_var(width);
    fz_var(page);

    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, pageNumber);
        if (!page) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page");
        }
        fz_rect bounds = fz_bound_page(ctx, page);
        width = static_cast<int>(bounds.x1 - bounds.x0);
        fz_drop_page(ctx, page);
        page = nullptr;
    }
    fz_catch(ctx)
    {
        if (page) {
            fz_drop_page(ctx, page);
        }
        width = 0;
    }

    return width;
}

int MuPdfDocument::getPageHeightNative(int pageNumber)
{
    std::lock_guard<std::mutex> lock(m_renderMutex);
    
    if (!m_ctx || !m_doc)
        return 0;
    
    // Validate page number first
    if (pageNumber < 0 || pageNumber >= m_pageCount) {
        return 0;
    }

    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    
    // Additional safety check
    if (!ctx || !doc) {
        return 0;
    }
    
    int height = 0;
    fz_page *page = nullptr;
    
    fz_var(height);
    fz_var(page);

    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, pageNumber);
        if (!page) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page");
        }
        fz_rect bounds = fz_bound_page(ctx, page);
        height = static_cast<int>(bounds.y1 - bounds.y0);
        fz_drop_page(ctx, page);
        page = nullptr;
    }
    fz_catch(ctx)
    {
        if (page) {
            fz_drop_page(ctx, page);
        }
        height = 0;
    }

    return height;
}

int MuPdfDocument::getPageWidthEffective(int pageNumber, int zoom)
{
    if (!m_ctx || !m_doc)
        return 0;

    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    volatile int width = 0;

    fz_try(ctx)
    {
        volatile fz_page *page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, (fz_page*)page);
        int nativeWidth = static_cast<int>(bounds.x1 - bounds.x0);
        int nativeHeight = static_cast<int>(bounds.y1 - bounds.y0);
        fz_drop_page(ctx, (fz_page*)page);

        float baseScale = zoom / 100.0f;
        int scaledWidth = static_cast<int>(nativeWidth * baseScale);
        int scaledHeight = static_cast<int>(nativeHeight * baseScale);

        // Apply downsampling logic (same as renderPage)
        float downsampleScale = 1.0f;
        const float oversizeTolerance = 1.5f; // Allow 50% oversize before downsampling
        if (scaledWidth > m_maxWidth * oversizeTolerance || scaledHeight > m_maxHeight * oversizeTolerance)
        {
            float scaleX = static_cast<float>(m_maxWidth) / scaledWidth;
            float scaleY = static_cast<float>(m_maxHeight) / scaledHeight;
            downsampleScale = std::min(scaleX, scaleY);

            // Gradual scaling for zoom levels - avoid cliff effects
            if (baseScale > 1.0f) {
                float zoomFactor = std::min(baseScale, 3.5f); // Cap at 3.5x
                float detailScale = static_cast<float>(m_maxWidth * zoomFactor) / scaledWidth;
                downsampleScale = std::max(downsampleScale, detailScale);
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

    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    volatile int height = 0;

    fz_try(ctx)
    {
        volatile fz_page *page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, (fz_page*)page);
        int nativeWidth = static_cast<int>(bounds.x1 - bounds.x0);
        int nativeHeight = static_cast<int>(bounds.y1 - bounds.y0);
        fz_drop_page(ctx, (fz_page*)page);

        float baseScale = zoom / 100.0f;
        int scaledWidth = static_cast<int>(nativeWidth * baseScale);
        int scaledHeight = static_cast<int>(nativeHeight * baseScale);

        // Apply downsampling logic (same as renderPage)
        float downsampleScale = 1.0f;
        const float oversizeTolerance = 1.5f; // Allow 50% oversize before downsampling
        if (scaledWidth > m_maxWidth * oversizeTolerance || scaledHeight > m_maxHeight * oversizeTolerance)
        {
            float scaleX = static_cast<float>(m_maxWidth) / scaledWidth;
            float scaleY = static_cast<float>(m_maxHeight) / scaledHeight;
            downsampleScale = std::min(scaleX, scaleY);

            // Gradual scaling for zoom levels - avoid cliff effects
            if (baseScale > 1.0f) {
                float zoomFactor = std::min(baseScale, 3.5f); // Cap at 3.5x
                float detailScaleX = static_cast<float>(m_maxWidth * zoomFactor) / scaledWidth;
                float detailScaleY = static_cast<float>(m_maxHeight * zoomFactor) / scaledHeight;
                float detailScale = std::min(detailScaleX, detailScaleY);
                downsampleScale = std::max(downsampleScale, detailScale);
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
    // Stop any background prerendering first
    if (m_prerenderThread.joinable()) {
        m_prerenderActive = false;
        m_prerenderThread.join();
    }
    
    m_doc.reset();
    m_ctx.reset();
    m_prerenderDoc.reset();
    m_prerenderCtx.reset();
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache.clear();
    }
}

bool MuPdfDocument::isPageValid(int pageNumber)
{
    std::lock_guard<std::mutex> lock(m_renderMutex);
    
    if (!m_ctx || !m_doc) {
        return false;
    }
    
    if (pageNumber < 0 || pageNumber >= m_pageCount) {
        return false;
    }
    
    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    fz_page *page = nullptr;
    bool isValid = false;
    
    fz_var(page);
    fz_var(isValid);
    
    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, pageNumber);
        if (page) {
            // Try to get page bounds to verify it's not corrupted
            fz_rect bounds = fz_bound_page(ctx, page);
            // Check if bounds are reasonable
            if (bounds.x1 > bounds.x0 && bounds.y1 > bounds.y0 && 
                bounds.x1 - bounds.x0 < 100000 && bounds.y1 - bounds.y0 < 100000) {
                isValid = true;
            }
            fz_drop_page(ctx, page);
        }
    }
    fz_catch(ctx)
    {
        if (page) {
            fz_drop_page(ctx, page);
        }
        isValid = false;
        
        // Log the specific error for debugging
        const char* fzError = fz_caught_message(ctx);
        if (fzError) {
            std::cerr << "MuPdfDocument: Page " << pageNumber << " validation failed: " << fzError << std::endl;
        }
    }
    
    return isValid;
}

void MuPdfDocument::clearCache()
{
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_cache.clear();
}

void MuPdfDocument::prerenderPage(int pageNumber, int scale)
{
    // Validate page number
    if (pageNumber < 0 || pageNumber >= m_pageCount) {
        return;
    }
    
    // Check if already cached
    auto key = std::make_pair(pageNumber, scale);
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_cache.find(key) != m_cache.end()) {
            return; // Already cached
        }
    }
    
    // Use separate context and mutex for prerendering to avoid race conditions
    std::lock_guard<std::mutex> prerenderLock(m_prerenderMutex);
    
    if (!m_prerenderCtx || !m_prerenderDoc) {
        std::cerr << "Prerender context not available for page " << pageNumber << std::endl;
        return;
    }
    
    fz_context *ctx = m_prerenderCtx.get();
    fz_document *doc = m_prerenderDoc.get();
    
    try {
        // Calculate transform
        float baseScale = scale / 100.0f;
        float downsampleScale = 1.0f;
        fz_var(downsampleScale);
        
        // Apply downsampling logic similar to main renderPage
        volatile fz_page *tempPage = nullptr;
        fz_var(tempPage);
        fz_try(ctx)
        {
            tempPage = fz_load_page(ctx, doc, pageNumber);
            if (!tempPage) {
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page for prerender");
            }
            
            fz_rect bounds = fz_bound_page(ctx, (fz_page*)tempPage);
            int nativeWidth = static_cast<int>((bounds.x1 - bounds.x0) * baseScale);
            int nativeHeight = static_cast<int>((bounds.y1 - bounds.y0) * baseScale);
            
            const float oversizeTolerance = 1.5f;
            if (nativeWidth > m_maxWidth * oversizeTolerance || nativeHeight > m_maxHeight * oversizeTolerance)
            {
                float scaleX = static_cast<float>(m_maxWidth) / nativeWidth;
                float scaleY = static_cast<float>(m_maxHeight) / nativeHeight;
                downsampleScale = std::min(scaleX, scaleY);
                
                if (baseScale > 1.0f) {
                    float zoomFactor = std::min(baseScale, 3.5f);
                    float maxDetailScale = zoomFactor;
                    float detailScaleX = static_cast<float>(m_maxWidth * maxDetailScale) / nativeWidth;
                    float detailScaleY = static_cast<float>(m_maxHeight * maxDetailScale) / nativeHeight;
                    float detailScale = std::min(detailScaleX, detailScaleY);
                    downsampleScale = std::max(downsampleScale, detailScale);
                }
            }
            fz_drop_page(ctx, (fz_page*)tempPage);
        }
        fz_catch(ctx)
        {
            if (tempPage) fz_drop_page(ctx, (fz_page*)tempPage);
            throw std::runtime_error("Error calculating page bounds for prerender");
        }
        
        // Render with the prerender context
        fz_matrix transform = fz_scale(baseScale * downsampleScale, baseScale * downsampleScale);
        volatile fz_pixmap *pix = nullptr;
        fz_var(pix);
        std::vector<unsigned char> buffer;
        int width, height;
        fz_var(width);
        fz_var(height);
        
        fz_try(ctx)
        {
            volatile fz_page *page = fz_load_page(ctx, doc, pageNumber);
            if (!page) {
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page for prerender");
            }

            pix = fz_new_pixmap_from_page(ctx, (fz_page*)page, transform, fz_device_rgb(ctx), 0);
            if (!pix) {
                fz_drop_page(ctx, (fz_page*)page);
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to create pixmap for prerender");
            }

            fz_drop_page(ctx, (fz_page*)page);

            width = fz_pixmap_width(ctx, (fz_pixmap*)pix);
            height = fz_pixmap_height(ctx, (fz_pixmap*)pix);

            size_t dataSize = width * height * 3;
            buffer.resize(dataSize);
            memcpy(buffer.data(), fz_pixmap_samples(ctx, (fz_pixmap*)pix), dataSize);

            fz_drop_pixmap(ctx, (fz_pixmap*)pix);
            pix = nullptr;
        }
        fz_catch(ctx)
        {
            if (pix) fz_drop_pixmap(ctx, (fz_pixmap*)pix);
            
            const char* fzError = fz_caught_message(ctx);
            std::string errorMsg = "Error prerendering page " + std::to_string(pageNumber);
            if (fzError && strlen(fzError) > 0) {
                errorMsg += ": " + std::string(fzError);
            }
            throw std::runtime_error(errorMsg);
        }

        // Cache the result
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            m_cache[key] = std::make_tuple(buffer, width, height);
        }
        
        std::cout << "Prerendered page " << pageNumber << " at " << scale << "% zoom" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to prerender page " << pageNumber << ": " << e.what() << std::endl;
        
        // If prerendering fails due to corruption or image format errors, don't retry
        if (strstr(e.what(), "cycle in tree") || strstr(e.what(), "PDF corruption") || 
            strstr(e.what(), "unknown image file format") || strstr(e.what(), "image format")) {
            std::cerr << "Page " << pageNumber << " marked as problematic - skipping future prerender attempts" << std::endl;
        }
    }
}

void MuPdfDocument::prerenderAdjacentPages(int currentPage, int scale)
{
    // Prerender next page
    if (currentPage + 1 < m_pageCount) {
        prerenderPage(currentPage + 1, scale);
    }
    
    // Prerender previous page  
    if (currentPage - 1 >= 0) {
        prerenderPage(currentPage - 1, scale);
    }
    
    // For better user experience, also prerender the page after next
    if (currentPage + 2 < m_pageCount) {
        prerenderPage(currentPage + 2, scale);
    }
}

void MuPdfDocument::prerenderAdjacentPagesAsync(int currentPage, int scale)
{
    // Stop any existing background prerendering
    if (m_prerenderThread.joinable()) {
        m_prerenderActive = false;
        m_prerenderThread.join();
    }
    
    // Start new background prerendering
    m_prerenderActive = true;
    m_prerenderThread = std::thread([this, currentPage, scale]() {
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
        
        m_prerenderActive = false;
    });
}

fz_rect MuPdfDocument::getPageContentBounds(int pageNumber)
{
    std::lock_guard<std::mutex> lock(m_renderMutex);
    
    if (!m_ctx || !m_doc)
        return fz_empty_rect;
    
    // Validate page number first
    if (pageNumber < 0 || pageNumber >= m_pageCount) {
        return fz_empty_rect;
    }

    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    
    fz_rect contentBounds = fz_empty_rect;
    fz_page *page = nullptr;
    fz_device *device = nullptr;
    
    fz_var(page);
    fz_var(device);
    fz_var(contentBounds);

    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, pageNumber);
        if (!page) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page");
        }
        
        // Create a bounding box device to capture content bounds
        device = fz_new_bbox_device(ctx, &contentBounds);
        if (!device) {
            fz_drop_page(ctx, page);
            fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to create bounding box device");
        }
        
        // Run the page content through the device to get content bounds
        fz_run_page(ctx, page, device, fz_identity, nullptr);
        
        fz_close_device(ctx, device);
        fz_drop_device(ctx, device);
        device = nullptr;
        
        fz_drop_page(ctx, page);
        page = nullptr;
    }
    fz_catch(ctx)
    {
        if (device) {
            fz_close_device(ctx, device);
            fz_drop_device(ctx, device);
        }
        if (page) {
            fz_drop_page(ctx, page);
        }
        contentBounds = fz_empty_rect;
    }

    return contentBounds;
}


