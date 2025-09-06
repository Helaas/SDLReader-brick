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

    fz_document *doc = nullptr;
    fz_var(doc);
    
    fz_try(ctx)
    {
        doc = fz_open_document(ctx, filePath.c_str());
    }
    fz_catch(ctx)
    {
        std::cerr << "Failed to open document: " << filePath << "\n";
        return false;
    }

    m_doc = std::unique_ptr<fz_document, DocumentDeleter>(doc, DocumentDeleter{ctx});
    m_pageCount = fz_count_pages(ctx, doc);
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

    // Calculate transform for initial rendering
    float baseScale = zoom / 100.0f;
    
    // Check cache using exact zoom level for cache key
    auto key = std::make_pair(pageNumber, zoom);
    
    std::cout << "Render page " << pageNumber << ": zoom=" << zoom << "% (MuPDF 1.26.7 improved)" << std::endl;
    
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

    // Initial transform - render at requested scale
    fz_matrix transform = fz_scale(baseScale, baseScale);
    fz_pixmap *originalPix = nullptr;
    fz_pixmap *finalPix = nullptr;
    std::vector<unsigned char> buffer;
    
    fz_var(originalPix);
    fz_var(finalPix);

    fz_try(ctx)
    {
        fz_page *page = fz_load_page(ctx, doc, pageNumber);
        if (!page) {
            throw std::runtime_error("Failed to load page " + std::to_string(pageNumber) + " for rendering");
        }

        // Render at the requested scale
        originalPix = fz_new_pixmap_from_page(ctx, page, transform, fz_device_rgb(ctx), 0);
        if (!originalPix) {
            fz_drop_page(ctx, page);
            throw std::runtime_error("Failed to create pixmap for page " + std::to_string(pageNumber));
        }

        fz_drop_page(ctx, page);

        int originalWidth = fz_pixmap_width(ctx, originalPix);
        int originalHeight = fz_pixmap_height(ctx, originalPix);
        
        std::cout << "Original rendered size: " << originalWidth << "x" << originalHeight << std::endl;

        // For zoom levels > 100%, allow larger renders to show detail
        // Calculate effective max size based on zoom level
        int effectiveMaxWidth = m_maxWidth;
        int effectiveMaxHeight = m_maxHeight;
        
        if (zoom > 100) {
            // Scale max render size proportionally to zoom level (up to 4x for very high zoom)
            float zoomFactor = std::min(zoom / 100.0f, 4.0f);
            effectiveMaxWidth = static_cast<int>(m_maxWidth * zoomFactor);
            effectiveMaxHeight = static_cast<int>(m_maxHeight * zoomFactor);
            std::cout << "High zoom detected (" << zoom << "%), using expanded max size: " 
                      << effectiveMaxWidth << "x" << effectiveMaxHeight << std::endl;
        }

        // Check if we need to scale down to fit within effective max size constraints
        if (originalWidth > effectiveMaxWidth || originalHeight > effectiveMaxHeight)
        {
            float scaleX = static_cast<float>(effectiveMaxWidth) / originalWidth;
            float scaleY = static_cast<float>(effectiveMaxHeight) / originalHeight;
            float scaleDown = std::min(scaleX, scaleY);
            
            int newWidth = static_cast<int>(originalWidth * scaleDown);
            int newHeight = static_cast<int>(originalHeight * scaleDown);
            
            std::cout << "Scaling down from " << originalWidth << "x" << originalHeight 
                      << " to " << newWidth << "x" << newHeight 
                      << " (scale factor: " << scaleDown << ")" << std::endl;
            
            // Use fz_scale_pixmap to scale down - this is the new feature!
            finalPix = fz_scale_pixmap(ctx, originalPix, 0, 0, newWidth, newHeight, nullptr);
            if (!finalPix) {
                throw std::runtime_error("Failed to scale pixmap for page " + std::to_string(pageNumber));
            }
            
            // Clean up original pixmap
            fz_drop_pixmap(ctx, originalPix);
            originalPix = nullptr;
        }
        else
        {
            // No scaling needed, use original pixmap
            finalPix = originalPix;
            originalPix = nullptr; // Transfer ownership
            std::cout << "No scaling needed, using original size" << std::endl;
        }

        width = fz_pixmap_width(ctx, finalPix);
        height = fz_pixmap_height(ctx, finalPix);

        std::cout << "Final output size: " << width << "x" << height << std::endl;

        size_t dataSize = width * height * 3;
        buffer.resize(dataSize);
        memcpy(buffer.data(), fz_pixmap_samples(ctx, finalPix), dataSize);

        fz_drop_pixmap(ctx, finalPix);
        finalPix = nullptr;
    }
    fz_catch(ctx)
    {
        if (finalPix && finalPix != originalPix)
            fz_drop_pixmap(ctx, finalPix);
        if (originalPix)
            fz_drop_pixmap(ctx, originalPix);
        
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
        int nativeWidth = static_cast<int>(bounds.x1 - bounds.x0);
        fz_drop_page(ctx, page);
        page = nullptr;
        
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
        if (page) {
            fz_drop_page(ctx, page);
        }
        width = 0;
    }

    return width;
}

int MuPdfDocument::getPageHeightEffective(int pageNumber, int zoom)
{
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
        int nativeWidth = static_cast<int>(bounds.x1 - bounds.x0);
        int nativeHeight = static_cast<int>(bounds.y1 - bounds.y0);
        fz_drop_page(ctx, page);
        page = nullptr;
        
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
        if (page) {
            fz_drop_page(ctx, page);
        }
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
