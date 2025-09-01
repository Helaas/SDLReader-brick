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

    fz_context *ctx = m_ctx.get();
    fz_document *doc = m_doc.get();

    // Calculate transform including downsampling
    float baseScale = zoom / 100.0f;
    float downsampleScale = 1.0f;
    
    // Pre-calculate if we need downsampling to avoid fz_scale_pixmap
    fz_page *tempPage = nullptr;
    fz_try(ctx)
    {
        tempPage = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, tempPage);
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
                // Higher zoom = more detail allowed, up to 2x window size
                float zoomFactor = std::min(baseScale, 2.0f); // Cap at 2x
                float maxDetailScale = zoomFactor;
                float detailScaleX = static_cast<float>(m_maxWidth * maxDetailScale) / nativeWidth;
                float detailScaleY = static_cast<float>(m_maxHeight * maxDetailScale) / nativeHeight;
                float detailScale = std::min(detailScaleX, detailScaleY);
                
                // Use the more permissive scale, but ensure smooth transitions
                downsampleScale = std::max(downsampleScale, detailScale);
            }
        }
        fz_drop_page(ctx, tempPage);
    }
    fz_catch(ctx)
    {
        if (tempPage) fz_drop_page(ctx, tempPage);
        throw std::runtime_error("Error calculating page bounds");
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
    fz_pixmap *pix = nullptr;
    std::vector<unsigned char> buffer;

    fz_try(ctx)
    {
        fz_page *page = fz_load_page(ctx, doc, pageNumber);

        pix = fz_new_pixmap_from_page(ctx, page, transform, fz_device_rgb(ctx), 0);

        fz_drop_page(ctx, page);

        width = fz_pixmap_width(ctx, pix);
        height = fz_pixmap_height(ctx, pix);

        // No post-processing scaling needed - downsampling applied in transform

        size_t dataSize = width * height * 3;
        buffer.resize(dataSize);
        memcpy(buffer.data(), fz_pixmap_samples(ctx, pix), dataSize);

        fz_drop_pixmap(ctx, pix);
        pix = nullptr;
    }
    fz_catch(ctx)
    {
        if (pix)
            fz_drop_pixmap(ctx, pix);
        throw std::runtime_error("Error rendering page");
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
        fz_page *page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, page);
        width = static_cast<int>(bounds.x1 - bounds.x0);
        fz_drop_page(ctx, page);
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
        fz_page *page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, page);
        height = static_cast<int>(bounds.y1 - bounds.y0);
        fz_drop_page(ctx, page);
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
        fz_page *page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, page);
        int nativeWidth = static_cast<int>(bounds.x1 - bounds.x0);
        fz_drop_page(ctx, page);
        
        // Apply zoom
        float baseScale = zoom / 100.0f;
        int scaledWidth = static_cast<int>(nativeWidth * baseScale);
        
        // Apply downsampling logic matching renderPage
        if (scaledWidth > m_maxWidth)
        {
            float downsampleScale = static_cast<float>(m_maxWidth) / scaledWidth;
            
            // Gradual scaling for zoom levels - avoid cliff effects
            if (baseScale > 1.0f) {
                float zoomFactor = std::min(baseScale, 2.0f); // Cap at 2x
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
        fz_page *page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, page);
        int nativeWidth = static_cast<int>(bounds.x1 - bounds.x0);
        int nativeHeight = static_cast<int>(bounds.y1 - bounds.y0);
        fz_drop_page(ctx, page);
        
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
                float zoomFactor = std::min(baseScale, 2.0f); // Cap at 2x
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
