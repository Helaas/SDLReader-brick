#include "page_preloader.h"
#include "document.h"
#include "mupdf_document.h"
#include "app.h"
#include <iostream>
#include <algorithm>
#include <chrono>

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
    
    // Clear the queue to prevent processing more requests
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::queue<PreloadRequest> empty;
        std::swap(m_preloadQueue, empty);
    }
    
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
            // Check if app and document are still valid before accessing them
            if (!m_app || !m_document) {
                return; // App or document no longer available
            }
            
            // Lock the document mutex for thread-safe access
            std::lock_guard<std::mutex> docLock(m_app->getDocumentMutex());
            if (!m_document) {
                return; // Document might have been nulled between checks
            }
            totalPages = m_document->getPageCount();
        }
        
        // Add current page first, then upcoming pages with priority
        PreloadRequest currentRequest;
        currentRequest.pageNumber = currentPage;
        currentRequest.scale = scale;
        currentRequest.priority = 0; // Current page has highest priority
        m_preloadQueue.push(currentRequest);
        
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
    
    // Clean up old cache entries in background, but not too frequently
    // to avoid cache thrashing during rapid navigation
    static auto lastCleanup = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastCleanup = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCleanup);
    
    if (timeSinceLastCleanup.count() >= 1000) { // Only cleanup every 1 second
        cleanupOldCacheEntries(currentPage, scale);
        lastCleanup = now;
    }
}

void PagePreloader::requestBidirectionalPreload(int currentPage, int scale) {
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
    
    // Clear old queue and add new bidirectional requests
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        
        // Clear existing queue to prioritize new requests
        std::queue<PreloadRequest> empty;
        std::swap(m_preloadQueue, empty);
        
        // Get total pages for bounds checking
        int totalPages;
        {
            // Lock the document mutex for thread-safe access
            std::lock_guard<std::mutex> docLock(m_app->getDocumentMutex());
            totalPages = m_document->getPageCount();
        }
        
        // Add requests for previous pages (when zoom changes, we want to cache backwards too)
        for (int i = 1; i <= m_preloadCount; ++i) {
            int prevPage = currentPage - i;
            if (prevPage >= 0) {
                PreloadRequest request;
                request.pageNumber = prevPage;
                request.scale = scale;
                request.priority = i + 100; // Lower priority than forward pages
                m_preloadQueue.push(request);
            }
        }
        
        // Add requests for upcoming pages (higher priority)
        for (int i = 1; i <= m_preloadCount; ++i) {
            int nextPage = currentPage + i;
            if (nextPage < totalPages) {
                PreloadRequest request;
                request.pageNumber = nextPage;
                request.scale = scale;
                request.priority = i; // Higher priority for forward pages
                m_preloadQueue.push(request);
            }
        }
    }
    
    m_queueCondition.notify_all();
    
    // Clean up old cache entries in background, but not too frequently  
    // to avoid cache thrashing during rapid navigation
    static auto lastBidirectionalCleanup = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastCleanup = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBidirectionalCleanup);
    
    if (timeSinceLastCleanup.count() >= 1000) { // Only cleanup every 1 second
        cleanupOldCacheEntries(currentPage, scale);
        lastBidirectionalCleanup = now;
    }
}

std::shared_ptr<PagePreloader::PreloadedPage> PagePreloader::getPreloadedPage(int pageNumber, int scale) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    std::string key = getCacheKey(pageNumber, scale);
    auto it = m_preloadedPages.find(key);
    
    if (it != m_preloadedPages.end()) {
        auto page = it->second;
        // Additional safety check: ensure the page data is valid and complete
        if (page && !page->pixelData.empty() && page->width > 0 && page->height > 0) {
            return page;
        } else {
            // Page exists but data is invalid, remove it from cache
            std::cerr << "PagePreloader: Found invalid cached page " << pageNumber 
                      << ", removing from cache" << std::endl;
            m_preloadedPages.erase(it);
        }
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
        
        // Additional check after unlocking - make sure we're still running
        // and that app/document are still valid
        if (!m_running || !m_app || !m_document) {
            continue;
        }
        
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
            // Check if app and document are still valid
            if (!m_app || !m_document) {
                return; // App or document no longer available
            }
            
            // Lock the document mutex for thread-safe access
            std::lock_guard<std::mutex> docLock(m_app->getDocumentMutex());
            if (!m_document) {
                return; // Document might have been nulled between checks
            }
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
            // Check if app and document are still valid
            if (!m_app || !m_document) {
                return; // App or document no longer available
            }
            
            // Lock the document mutex to ensure thread-safe access
            std::lock_guard<std::mutex> docLock(m_app->getDocumentMutex());
            if (!m_document) {
                return; // Document might have been nulled between checks
            }
            
            try {
                // Additional check right before rendering
                if (!m_running || !m_app || !m_document) {
                    return; // Exit if anything became invalid
                }
                
                // Check page bounds
                int totalPages = m_document->getPageCount();
                if (request.pageNumber < 0 || request.pageNumber >= totalPages) {
                    std::cerr << "PagePreloader: Page " << request.pageNumber 
                              << " is out of bounds (0-" << (totalPages - 1) << ")" << std::endl;
                    return;
                }
                
                pixelData = m_document->renderPage(
                    request.pageNumber, width, height, request.scale
                );
                
                // Validate the render result
                if (pixelData.empty() || width <= 0 || height <= 0) {
                    std::cerr << "PagePreloader: Invalid render result for page " << request.pageNumber 
                              << " (size: " << pixelData.size() << ", dims: " << width << "x" << height << ")" << std::endl;
                    return;
                }
                
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
        
        // Validate rendered data before caching
        if (pixelData.empty() || width <= 0 || height <= 0) {
            std::cerr << "PagePreloader: Invalid render data for page " << request.pageNumber 
                      << ", skipping cache insertion" << std::endl;
            return;
        }
        
        // Additional size validation to prevent memory corruption
        size_t expectedSize = static_cast<size_t>(width) * height * 3; // RGB format (3 bytes per pixel)
        if (pixelData.size() != expectedSize) {
            std::cerr << "PagePreloader: Pixel data size mismatch for page " << request.pageNumber 
                      << " (expected: " << expectedSize << ", got: " << pixelData.size() << ")" << std::endl;
            return;
        }
        
        // Create preloaded page object with explicit copy to avoid race conditions
        auto preloadedPage = std::make_shared<PreloadedPage>();
        preloadedPage->pixelData.reserve(pixelData.size());
        preloadedPage->pixelData = pixelData; // Copy instead of move to be safer
        preloadedPage->width = width;
        preloadedPage->height = height;
        preloadedPage->scale = request.scale;
        preloadedPage->pageNumber = request.pageNumber;
        
        // Add to cache with additional validation
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            
            // Double-check we're still running before modifying cache
            if (!m_running) {
                return;
            }
            
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
    
    // Be more conservative with cleanup to avoid race conditions
    // Keep a larger range around current page to account for rapid navigation
    const int keepRange = (m_preloadCount * 2) + 5; // Much larger safety margin
    
    auto it = m_preloadedPages.begin();
    while (it != m_preloadedPages.end()) {
        int pageNumber = it->second->pageNumber;
        int pageScale = it->second->scale;
        
        // Only remove pages that are really far away or wrong scale
        // Be more lenient to prevent cache thrashing during rapid navigation
        if (std::abs(pageNumber - currentPage) > keepRange || pageScale != scale) {
            std::cout << "PagePreloader: Cleaning up page " << pageNumber 
                      << " (too far from current page " << currentPage << ", range=" << keepRange << ")" << std::endl;
            it = m_preloadedPages.erase(it);
        } else {
            ++it;
        }
    }
}
