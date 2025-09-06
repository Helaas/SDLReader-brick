#ifndef PAGE_PRELOADER_STUB_H
#define PAGE_PRELOADER_STUB_H

#include <memory>
#include <vector>
#include <cstdint>

class Document;
class App;

/**
 * @brief Stub PagePreloader that does nothing
 * 
 * This is a no-op replacement for the complex PagePreloader to eliminate
 * race conditions while maintaining API compatibility.
 * Modern MuPDF 1.26.7 has excellent built-in caching.
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

    PagePreloader(App* app, Document* document) { (void)app; (void)document; }
    ~PagePreloader() = default;
    
    void start() { /* no-op */ }
    void stop() { /* no-op */ }
    void requestPreload(int currentPage, int scale) { (void)currentPage; (void)scale; }
    void requestBidirectionalPreload(int currentPage, int scale) { (void)currentPage; (void)scale; }
    
    std::shared_ptr<PreloadedPage> getPreloadedPage(int pageNumber, int scale) { 
        (void)pageNumber; (void)scale; 
        return nullptr; // Always return null - forces direct rendering
    }
    
    void clearCache() { /* no-op */ }
    void setPreloadCount(int count) { (void)count; }
};

#endif // PAGE_PRELOADER_STUB_H
