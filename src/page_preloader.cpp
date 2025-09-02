#include "page_preloader.h"
#include "document.h"
#include "mupdf_document.h"
#include "app.h"
#include <iostream>
#include <algorithm>

PagePreloader::PagePreloader(App* app, Document* document) 
    : m_app(app), m_document(document)
{
    if (!m_app) {
        throw std::invalid_argument("App cannot be null");
    }
    if (!m_document) {
        throw std::invalid_argument("Document cannot be null");
    }
}

PagePreloader::~PagePreloader() {
    stop();
}

void PagePreloader::start() {
    if (m_running) {
        return; // Already running
    }
    
    m_running = true;
    m_workerThread = std::thread(&PagePreloader::preloadWorker, this);
    std::cout << "PagePreloader: Started background preloader thread" << std::endl;
}

void PagePreloader::stop() {
    if (!m_running) {
        return; // Already stopped
    }
    
    m_running = false;
    m_queueCondition.notify_all();
    
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    
    std::cout << "PagePreloader: Stopped background preloader thread" << std::endl;
}

void PagePreloader::requestPreload(int currentPage, int scale) {
    if (!m_running) {
        return; // Not running, ignore request
    }
    
    if (!m_document) {
        return; // No document available
    }
    
    // Avoid duplicate requests
    {
        std::lock_guard<std::mutex> lock(m_lastRequestMutex);
        if (currentPage == m_lastCurrentPage && scale == m_lastScale) {
            return; // Same request as last time
        }
        m_lastCurrentPage = currentPage;
        m_lastScale = scale;
    }
    
    if (!m_running) {
        return;
    }
    
    // Clear old queue and add new requests
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        
        // Clear existing queue to prioritize new requests
        std::queue<PreloadRequest> empty;
        std::swap(m_preloadQueue, empty);
        
        // Add requests for upcoming pages with priority
        int totalPages;
        {
            // Lock the document mutex for thread-safe access
            std::lock_guard<std::mutex> docLock(m_app->getDocumentMutex());
            totalPages = m_document->getPageCount();
        }
        
        for (int i = 1; i <= m_preloadCount; ++i) {
            int nextPage = currentPage + i;
            if (nextPage < totalPages) {
                PreloadRequest request;
                request.pageNumber = nextPage;
                request.scale = scale;
                request.priority = i; // Lower priority for pages further ahead
                m_preloadQueue.push(request);
            }
        }
    }
    
    m_queueCondition.notify_all();
    
    // Clean up old cache entries in background
    cleanupOldCacheEntries(currentPage, scale);
}

std::shared_ptr<PagePreloader::PreloadedPage> PagePreloader::getPreloadedPage(int pageNumber, int scale) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    std::string key = getCacheKey(pageNumber, scale);
    auto it = m_preloadedPages.find(key);
    
    if (it != m_preloadedPages.end()) {
        std::cout << "PagePreloader: Cache hit for page " << pageNumber << " at scale " << scale << std::endl;
        return it->second;
    }
    
    return nullptr;
}

void PagePreloader::clearCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_preloadedPages.clear();
    std::cout << "PagePreloader: Cache cleared" << std::endl;
}

void PagePreloader::preloadWorker() {
    std::cout << "PagePreloader: Worker thread started" << std::endl;
    
    while (m_running) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_queueCondition.wait(lock, [this] { 
            return !m_preloadQueue.empty() || !m_running; 
        });
        
        if (!m_running) break;
        
        if (m_preloadQueue.empty()) continue;
        
        PreloadRequest request = m_preloadQueue.front();
        m_preloadQueue.pop();
        lock.unlock();
        
        // Check if page is already cached
        std::string key = getCacheKey(request.pageNumber, request.scale);
        {
            std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
            if (m_preloadedPages.find(key) != m_preloadedPages.end()) {
                continue; // Already cached
            }
        }
        
        preloadPage(request);
    }
    
    std::cout << "PagePreloader: Worker thread stopped" << std::endl;
}

void PagePreloader::preloadPage(const PreloadRequest& request) {
    try {
        // Check if we're still running before doing expensive work
        if (!m_running) {
            return;
        }
        
        // Check if the page is valid before attempting to render it
        // This helps detect corrupted PDF pages early
        bool pageValid = true;
        if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document)) {
            // Lock the document mutex for thread-safe access
            std::lock_guard<std::mutex> docLock(m_app->getDocumentMutex());
            pageValid = muDoc->isPageValid(request.pageNumber);
        }
        
        if (!pageValid) {
            std::cerr << "PagePreloader: Skipping invalid/corrupted page " << request.pageNumber << std::endl;
            return;
        }
        
        std::cout << "PagePreloader: Preloading page " << request.pageNumber 
                  << " at scale " << request.scale << std::endl;
        
        // Render the page with thread-safe document access
        int width, height;
        std::vector<uint8_t> pixelData;
        {
            // Lock the document mutex to ensure thread-safe access
            std::lock_guard<std::mutex> docLock(m_app->getDocumentMutex());
            try {
                pixelData = m_document->renderPage(
                    request.pageNumber, width, height, request.scale
                );
            } catch (const std::exception& e) {
                std::cerr << "PagePreloader: Error preloading page " << request.pageNumber 
                          << ": " << e.what() << std::endl;
                return; // Skip this page and continue
            }
        }
        
        // Check again if we're still running after the potentially long render operation
        if (!m_running) {
            return;
        }
        
        // Create preloaded page object
        auto preloadedPage = std::make_shared<PreloadedPage>();
        preloadedPage->pixelData = std::move(pixelData);
        preloadedPage->width = width;
        preloadedPage->height = height;
        preloadedPage->scale = request.scale;
        preloadedPage->pageNumber = request.pageNumber;
        
        // Add to cache
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            
            // Enforce cache size limit
            if (m_preloadedPages.size() >= MAX_CACHE_SIZE) {
                // Remove the first (oldest) entry
                auto it = m_preloadedPages.begin();
                std::cout << "PagePreloader: Cache full, removing page " 
                          << it->second->pageNumber << std::endl;
                m_preloadedPages.erase(it);
            }
            
            std::string key = getCacheKey(request.pageNumber, request.scale);
            m_preloadedPages[key] = preloadedPage;
        }
        
        std::cout << "PagePreloader: Successfully preloaded page " << request.pageNumber << std::endl;
        
    } catch (const std::exception& e) {
        // Only log error if we're still running (not shutting down)
        if (m_running) {
            std::cerr << "PagePreloader: Error preloading page " << request.pageNumber 
                      << ": " << e.what() << std::endl;
        }
    }
}

std::string PagePreloader::getCacheKey(int pageNumber, int scale) const {
    return std::to_string(pageNumber) + "_" + std::to_string(scale);
}

void PagePreloader::cleanupOldCacheEntries(int currentPage, int scale) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    // Remove pages that are too far behind or ahead
    const int keepRange = m_preloadCount + 2; // Keep a few extra pages around current page
    
    auto it = m_preloadedPages.begin();
    while (it != m_preloadedPages.end()) {
        int pageNumber = it->second->pageNumber;
        int pageScale = it->second->scale;
        
        // Remove if page is too far from current page or wrong scale
        if (std::abs(pageNumber - currentPage) > keepRange || pageScale != scale) {
            std::cout << "PagePreloader: Cleaning up page " << pageNumber 
                      << " (too far from current page " << currentPage << ")" << std::endl;
            it = m_preloadedPages.erase(it);
        } else {
            ++it;
        }
    }
}
