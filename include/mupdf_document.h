#ifndef MUPDF_DOCUMENT_H
#define MUPDF_DOCUMENT_H

#include "document.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <mupdf/fitz.h>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

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
class MuPdfDocument : public Document
{
public:
    MuPdfDocument();
    ~MuPdfDocument() override;

    std::vector<unsigned char> renderPage(int page, int& width, int& height, int scale) override;
    std::vector<uint32_t> renderPageARGB(int page, int& width, int& height, int scale);
    int getPageWidthNative(int page) override;
    int getPageHeightNative(int page) override;
    int getPageWidthEffective(int page, int zoom);
    int getPageHeightEffective(int page, int zoom);
    std::pair<int, int> getPageDimensionsEffective(int page, int zoom);
    bool tryGetCachedPageARGB(int page, int scale, std::vector<uint32_t>& buffer, int& width, int& height);
    void requestPageRenderAsync(int page, int scale);
    bool open(const std::string& filePath) override;
    bool reopenWithCSS(const std::string& css); // Reopen document with new CSS
    int getPageCount() const override;
    void setMaxRenderSize(int width, int height);
    void close() override;

    // Clear the render cache
    void clearCache();

    // Cancel any ongoing background prerendering
    void cancelPrerendering();

    // Check if background prerendering is currently active
    bool isPrerenderingActive() const
    {
        return m_prerenderActive;
    }

    // Prerender pages for faster page changes
    void prerenderPage(int pageNumber, int scale);
    void prerenderAdjacentPages(int currentPage, int scale);
    void prerenderAdjacentPagesAsync(int currentPage, int scale);

    // CSS styling for documents (EPUB/MOBI)
    void setUserCSSBeforeOpen(const std::string& css); // Set CSS before opening document
    std::string getUserCSS() const
    {
        return m_userCSS;
    }

    // Get the MuPDF context for font loader installation
    fz_context* getContext()
    {
        return m_ctx.get();
    }

private:
    // Internal open method with context reuse option to avoid TG5040 crash
    bool open(const std::string& filePath, bool reuseContexts);

    // Use smart pointers to manage MuPDF types safely
    struct ContextDeleter
    {
        void operator()(fz_context* ctx) const
        {
            if (ctx)
            {
                std::cout.flush();
                // NOTE: We do NOT call fz_drop_context() here because:
                // 1. MuPDF's fz_drop_context() can call exit() if there are accumulated errors/warnings
                // 2. This causes the entire process to terminate unexpectedly when returning to file browser
                // 3. Contexts will be cleaned up when the process exits
                // 4. This is safe because MuPDF contexts are designed to persist for the app lifetime
                std::cout.flush();
                // fz_drop_context(ctx); // COMMENTED OUT - causes exit() in browse mode
            }
        }
    };
    struct DocumentDeleter
    {
        fz_context* ctx = nullptr;
        DocumentDeleter() noexcept = default;
        DocumentDeleter(fz_context* c) noexcept : ctx(c)
        {
        }
        void operator()(fz_document* doc) const
        {
            if (doc && ctx)
            {
                std::cout.flush();
                fz_drop_document(ctx, doc);
                std::cout.flush();
            }
        }
    };
    struct PixmapDeleter
    {
        fz_context* ctx{};
        void operator()(fz_pixmap* pix) const
        {
            if (pix)
                fz_drop_pixmap(ctx, pix);
        }
    };

    struct DisplayListDeleter
    {
        fz_context* ctx;
        DisplayListDeleter() noexcept : ctx(nullptr)
        {
        }
        explicit DisplayListDeleter(fz_context* context) noexcept : ctx(context)
        {
        }
        void operator()(fz_display_list* list) const
        {
            if (list)
                fz_drop_display_list(ctx, list);
        }
    };

    struct PageDisplayData
    {
        std::unique_ptr<fz_display_list, DisplayListDeleter> displayList;
        fz_rect bounds{};
    };

    struct PageScaleInfo;

    std::unique_ptr<fz_context, ContextDeleter> m_ctx;
    std::unique_ptr<fz_document, DocumentDeleter> m_doc;

    // Separate context for background prerendering to avoid race conditions
    std::unique_ptr<fz_context, ContextDeleter> m_prerenderCtx;
    std::unique_ptr<fz_document, DocumentDeleter> m_prerenderDoc;
    std::mutex m_prerenderMutex; // Protects prerender context operations

    std::map<std::pair<int, int>, std::tuple<std::vector<unsigned char>, int, int>> m_cache;
    std::map<std::pair<int, int>, std::tuple<std::vector<uint32_t>, int, int>> m_argbCache;
    std::map<std::pair<int, int>, std::pair<int, int>> m_dimensionCache;
    std::mutex m_cacheMutex;
    std::mutex m_renderMutex; // Protects MuPDF context operations
    std::mutex m_pageDataMutex;
    int m_maxWidth = 2560;  // Increased for better performance at high zoom levels
    int m_maxHeight = 1920; // Increased for better performance at high zoom levels
    int m_pageCount = 0;
    std::vector<PageDisplayData> m_pageDisplayData;

    // Background prerendering support
    std::thread m_prerenderThread;
    std::atomic<bool> m_prerenderActive{false};
    std::chrono::steady_clock::time_point m_lastPrerenderTime;
    static constexpr int PRERENDER_COOLDOWN_MS = 50; // Minimum time between prerendering operations
    std::atomic<uint64_t> m_prerenderGeneration{0};

    // User CSS for styling documents
    std::string m_userCSS;

    // Store file path for reopening with new CSS
    std::string m_filePath;

    // Asynchronous current page rendering support
    std::thread m_asyncRenderThread;
    std::mutex m_asyncRenderMutex;
    std::condition_variable m_asyncRenderCv;
    std::deque<std::pair<int, int>> m_asyncRenderQueue;
    std::atomic<bool> m_asyncShutdown{false};
    std::atomic<bool> m_asyncWorkerRunning{false};

    // Helpers
    void ensureDisplayList(int pageNumber);
    PageScaleInfo computePageScaleInfoLocked(int pageNumber, int zoom);
    void resetDisplayCache();
    void joinAsyncRenderThread();
    bool isPrerenderRequestStale(uint64_t generationToken) const;
    void prerenderPageInternal(int pageNumber, int scale, uint64_t generationToken);
    void prerenderAdjacentPagesInternal(int currentPage, int scale, uint64_t generationToken);
    void asyncRenderWorker();
    bool isRenderableQueued(const std::pair<int, int>& key);
};

#endif // MUPDF_DOCUMENT_H
