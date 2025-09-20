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
 * - MOBI e-books (.mobi)
 * - And other formats supported by MuPDF
 */
class MuPdfDocument : public Document {
public:
    MuPdfDocument();
    ~MuPdfDocument() override;

    std::vector<unsigned char> renderPage(int page, int& width, int& height, int scale) override;
    std::vector<uint32_t> renderPageARGB(int page, int& width, int& height, int scale);
    int getPageWidthNative(int page) override;
    int getPageHeightNative(int page) override;
    int getPageWidthEffective(int page, int zoom);
    int getPageHeightEffective(int page, int zoom);
    bool open(const std::string& filePath) override;
    int getPageCount() const override;
    void setMaxRenderSize(int width, int height);
    void close() override;
    
    // Clear the render cache
    void clearCache();
    
    // Cancel any ongoing background prerendering
    void cancelPrerendering();
    
    // Check if background prerendering is currently active
    bool isPrerenderingActive() const { return m_prerenderActive; }
    
    // Prerender pages for faster page changes
    void prerenderPage(int pageNumber, int scale);
    void prerenderAdjacentPages(int currentPage, int scale);
    void prerenderAdjacentPagesAsync(int currentPage, int scale);

    // CSS styling for documents (EPUB/MOBI)
    void setUserCSS(const std::string& css);
    std::string getUserCSS() const { return m_userCSS; }

    // Get the MuPDF context for font loader installation
    fz_context* getContext() { return m_ctx.get(); }

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
    
    // Separate context for background prerendering to avoid race conditions
    std::unique_ptr<fz_context, ContextDeleter> m_prerenderCtx;
    std::unique_ptr<fz_document, DocumentDeleter> m_prerenderDoc;
    std::mutex m_prerenderMutex;  // Protects prerender context operations
    
    std::map<std::pair<int, int>, std::tuple<std::vector<unsigned char>, int, int>> m_cache;
    std::map<std::pair<int, int>, std::tuple<std::vector<uint32_t>, int, int>> m_argbCache;
    std::mutex m_cacheMutex;
    std::mutex m_renderMutex;  // Protects MuPDF context operations
    int m_maxWidth = 2560;   // Increased for better performance at high zoom levels
    int m_maxHeight = 1920;  // Increased for better performance at high zoom levels
    int m_pageCount = 0;
    
    // Background prerendering support
    std::thread m_prerenderThread;
    std::atomic<bool> m_prerenderActive{false};
    std::chrono::steady_clock::time_point m_lastPrerenderTime;
    static constexpr int PRERENDER_COOLDOWN_MS = 50; // Minimum time between prerendering operations
    
    // User CSS for styling documents
    std::string m_userCSS;
};

#endif // MUPDF_DOCUMENT_H
