#ifndef MUPDF_DOCUMENT_H
#define MUPDF_DOCUMENT_H

#include "document.h"
#include <mupdf/fitz.h>
#include <vector>
#include <memory>
#include <string>
#include <map>
#include <tuple>
#include <mutex>
#include <thread>
#include <atomic>

/**
 * @brief Document implementation using MuPDF library
 * 
 * This class handles multiple document formats through MuPDF's native support:
 * - PDF documents (.pdf)
 * - Comic book archives (.cbz, .zip containing images)
 * - EPUB documents (.epub)
 * - And other formats supported by MuPDF
 */
class MuPdfDocument : public Document {
public:
    MuPdfDocument();
    ~MuPdfDocument() override;

    std::vector<unsigned char> renderPage(int page, int& width, int& height, int scale) override;
    int getPageWidthNative(int page) override;
    int getPageHeightNative(int page) override;
    int getPageWidthEffective(int page, int zoom);
    int getPageHeightEffective(int page, int zoom);
    bool open(const std::string& filePath) override;
    int getPageCount() const override;
    void setMaxRenderSize(int width, int height);
    void close() override;
    
    // Validate if a page can be safely loaded (check for corruption)
    bool isPageValid(int pageNumber);
    
    // Clear the render cache
    void clearCache();
    
    // Prerender pages for faster page changes
    void prerenderPage(int pageNumber, int scale);
    void prerenderAdjacentPages(int currentPage, int scale);
    void prerenderAdjacentPagesAsync(int currentPage, int scale);

private:
    // Use smart pointers to manage MuPDF types safely
    struct ContextDeleter {
        void operator()(fz_context* ctx) const { if (ctx) fz_drop_context(ctx); }
    };
    struct DocumentDeleter {
        fz_context* ctx = nullptr;
        DocumentDeleter() noexcept = default;
        DocumentDeleter(fz_context* c) noexcept : ctx(c) {}
        void operator()(fz_document* doc) const { if (doc && ctx) fz_drop_document(ctx, doc); }
    };
    struct PixmapDeleter {
        fz_context* ctx{};
        void operator()(fz_pixmap* pix) const { if (pix) fz_drop_pixmap(ctx, pix); }
    };

    std::unique_ptr<fz_context, ContextDeleter> m_ctx;
    std::unique_ptr<fz_document, DocumentDeleter> m_doc;
    std::map<std::pair<int, int>, std::tuple<std::vector<unsigned char>, int, int>> m_cache;
    std::mutex m_cacheMutex;
    std::mutex m_renderMutex;  // Protects MuPDF context operations
    int m_maxWidth = 1024;
    int m_maxHeight = 768;
    int m_pageCount = 0;
    
    // Background prerendering support
    std::thread m_prerenderThread;
    std::atomic<bool> m_prerenderActive{false};
};

#endif // MUPDF_DOCUMENT_H
