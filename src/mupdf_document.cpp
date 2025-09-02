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

    volatile fz_document *doc = nullptr; // Use volatile to prevent longjmp clobbering
    fz_try(ctx)
    {
        doc = fz_open_document(ctx, filePath.c_str());
    }
    fz_catch(ctx)
    {
        std::cerr << "Failed to open document: " << filePath << "\n";
        return false;
    }

    m_doc = std::unique_ptr<fz_document, DocumentDeleter>((fz_document*)doc, DocumentDeleter{ctx});
    m_pageCount = fz_count_pages(ctx, (fz_document*)doc);
    return true;
}

std::vector<unsigned char> MuPdfDocument::renderPage(int pageNumber, int &width, int &height, int zoom)
{
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

    // Calculate transform including downsampling
    float baseScale = zoom / 100.0f;
    float downsampleScale = 1.0f;
    
    // Pre-calculate if we need downsampling to avoid fz_scale_pixmap
    volatile fz_page *tempPage = nullptr; // Use volatile to prevent longjmp clobbering
    fz_try(ctx)
    {
        // Check if page number is valid before attempting to load
        int totalPages = fz_count_pages(ctx, doc);
        if (pageNumber >= totalPages) {
            throw std::runtime_error("Page number out of range: " + std::to_string(pageNumber) + " >= " + std::to_string(totalPages));
        }
        
        tempPage = fz_load_page(ctx, doc, pageNumber);
        if (!tempPage) {
            throw std::runtime_error("Failed to load page " + std::to_string(pageNumber));
        }
        
        fz_rect bounds = fz_bound_page(ctx, (fz_page*)tempPage);
        int nativeWidth = static_cast<int>((bounds.x1 - bounds.x0) * baseScale);
        int nativeHeight = static_cast<int>((bounds.y1 - bounds.y0) * baseScale);
        
        // Only downsample if the zoomed image is larger than max size
        // This allows zooming in for detail up to the max render size
        if (nativeWidth > m_maxWidth || nativeHeight > m_maxHeight)
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

    // Check cache using effective scale for cache key to account for downsampling
    float effectiveScale = baseScale * downsampleScale;
    int effectiveZoom = static_cast<int>(effectiveScale * 100);
    auto key = std::make_pair(pageNumber, effectiveZoom);
    
    std::cout << "Render page " << pageNumber << ": zoom=" << zoom 
              << "%, baseScale=" << baseScale 
              << ", downsampleScale=" << downsampleScale 
              << ", effectiveZoom=" << effectiveZoom << "%" << std::endl;
    
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_cache.find(key);
        if (it != m_cache.end())
        {
            std::cout << "Cache HIT for page " << pageNumber << " at effective zoom " << effectiveZoom << "%" << std::endl;
            auto &[buffer, cachedWidth, cachedHeight] = it->second;
            width = cachedWidth;
            height = cachedHeight;
            return buffer;
        }
        std::cout << "Cache MISS for page " << pageNumber << " at effective zoom " << effectiveZoom << "%" << std::endl;
    }

    fz_matrix transform = fz_scale(baseScale * downsampleScale, baseScale * downsampleScale);
    volatile fz_pixmap *pix = nullptr; // Use volatile to prevent longjmp clobbering
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
    if (!m_ctx || !m_doc)
        return 0;

    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    volatile int width = 0; // volatile to survive longjmp across fz_try/fz_catch

    fz_try(ctx)
    {
        volatile fz_page *page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, (fz_page*)page);
        width = static_cast<int>(bounds.x1 - bounds.x0);
        fz_drop_page(ctx, (fz_page*)page);
    }
    fz_catch(ctx)
    {
        width = 0;
    }

    return width; // implicit cast from volatile int to int
}

int MuPdfDocument::getPageHeightNative(int pageNumber)
{
    if (!m_ctx || !m_doc)
        return 0;

    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    volatile int height = 0; // volatile to survive longjmp

    fz_try(ctx)
    {
        volatile fz_page *page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, (fz_page*)page);
        height = static_cast<int>(bounds.y1 - bounds.y0);
        fz_drop_page(ctx, (fz_page*)page);
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

    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    volatile int width = 0;

    fz_try(ctx)
    {
        volatile fz_page *page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, (fz_page*)page);
        int nativeWidth = static_cast<int>(bounds.x1 - bounds.x0);
        fz_drop_page(ctx, (fz_page*)page);
        
        // Apply zoom
        float baseScale = zoom / 100.0f;
        int scaledWidth = static_cast<int>(nativeWidth * baseScale);
        
        // Apply downsampling logic matching renderPage
        if (scaledWidth > m_maxWidth)
        {
            float downsampleScale = static_cast<float>(m_maxWidth) / scaledWidth;
            
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
    m_doc.reset();
    m_ctx.reset();
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache.clear();
    }
}

bool MuPdfDocument::isPageValid(int pageNumber)
{
    if (!m_ctx || !m_doc) {
        return false;
    }
    
    if (pageNumber < 0 || pageNumber >= m_pageCount) {
        return false;
    }
    
    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();
    volatile fz_page *page = nullptr;
    volatile bool isValid = false; // Make volatile to prevent longjmp issues
    
    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, pageNumber);
        if (page) {
            // Try to get page bounds to verify it's not corrupted
            fz_rect bounds = fz_bound_page(ctx, (fz_page*)page);
            // Check if bounds are reasonable
            if (bounds.x1 > bounds.x0 && bounds.y1 > bounds.y0 && 
                bounds.x1 - bounds.x0 < 100000 && bounds.y1 - bounds.y0 < 100000) {
                isValid = true;
            }
            fz_drop_page(ctx, (fz_page*)page);
        }
    }
    fz_catch(ctx)
    {
        if (page) {
            fz_drop_page(ctx, (fz_page*)page);
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
