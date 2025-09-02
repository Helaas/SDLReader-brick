#ifndef PAGE_PRELOADER_H
#define PAGE_PRELOADER_H

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
#include <cstdint>

class Document;
class App;

/**
 * @brief Preloads pages in a background thread for smooth page navigation
 * 
 * This class manages a worker thread that preloads upcoming pages in the background,
 * storing them in a cache for quick retrieval when the user navigates to them.
 */
class PagePreloader {
public:
    struct PreloadedPage {
        std::vector<uint8_t> pixelData;
        int width;
        int height;
        int scale;
        int pageNumber;
    };

    PagePreloader(App* app, Document* document);
    ~PagePreloader();
    
    /**
     * Start the preloader worker thread
     */
    void start();
    
    /**
     * Stop the preloader worker thread
     */
    void stop();
    
    /**
     * Request preloading of specific pages
     * @param currentPage The current page being viewed
     * @param scale The current scale/zoom level
     */
    void requestPreload(int currentPage, int scale);
    
    /**
     * Request bidirectional preloading when zoom changes
     * @param currentPage The current page being viewed
     * @param scale The new scale/zoom level
     */
    void requestBidirectionalPreload(int currentPage, int scale);
    
    /**
     * Get a preloaded page if available
     * @param pageNumber The page number to retrieve
     * @param scale The scale/zoom level
     * @return Preloaded page data if available, nullptr otherwise
     */
    std::shared_ptr<PreloadedPage> getPreloadedPage(int pageNumber, int scale);
    
    /**
     * Clear the entire cache (useful when document changes or memory cleanup)
     */
    void clearCache();
    
    /**
     * Set the number of pages to preload ahead of current page
     */
    void setPreloadCount(int count) { m_preloadCount = count; }

private:
    struct PreloadRequest {
        int pageNumber;
        int scale;
        int priority; // Lower numbers = higher priority
    };

    void preloadWorker();
    void preloadPage(const PreloadRequest& request);
    std::string getCacheKey(int pageNumber, int scale) const;
    void cleanupOldCacheEntries(int currentPage, int scale);
    
    App* m_app;
    Document* m_document;
    std::thread m_workerThread;
    std::queue<PreloadRequest> m_preloadQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    std::atomic<bool> m_running{false};
    
    // Cache for preloaded pages
    std::unordered_map<std::string, std::shared_ptr<PreloadedPage>> m_preloadedPages;
    std::mutex m_cacheMutex;
    
    // Configuration
    static const size_t MAX_CACHE_SIZE = 20; // Maximum number of pages to keep in cache (increased for rapid navigation)
    int m_preloadCount = 1; // Number of pages to preload ahead
    
    // Track last request to avoid duplicate work
    int m_lastCurrentPage = -1;
    int m_lastScale = -1;
    std::mutex m_lastRequestMutex;
};

#endif // PAGE_PRELOADER_H
