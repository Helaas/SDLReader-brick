#ifndef VIEWPORT_MANAGER_H
#define VIEWPORT_MANAGER_H

#include <SDL.h>
#include <chrono>
#include <memory>

// Forward declarations
class Document;
class Renderer;
class MuPdfDocument;

struct ViewportState
{
    int scrollX{0};
    int scrollY{0};
    int currentScale{100};
    int pageWidth{0};
    int pageHeight{0};
    int rotation{0};          // 0, 90, 180, 270 degrees
    bool mirrorH{false};      // Horizontal mirroring
    bool mirrorV{false};      // Vertical mirroring
    
    // Top alignment control when page height <= window height
    bool topAlignWhenFits{true};
    bool forceTopAlignNextRender{false}; // One-shot flag after page changes
};

class ViewportManager
{
public:
    ViewportManager(Renderer* renderer);
    ~ViewportManager() = default;

    // State accessors
    const ViewportState& getState() const { return m_state; }
    int getScrollX() const { return m_state.scrollX; }
    int getScrollY() const { return m_state.scrollY; }
    int getCurrentScale() const { return m_state.currentScale; }
    int getPageWidth() const { return m_state.pageWidth; }
    int getPageHeight() const { return m_state.pageHeight; }
    int getRotation() const { return m_state.rotation; }
    bool getMirrorH() const { return m_state.mirrorH; }
    bool getMirrorV() const { return m_state.mirrorV; }
    
    // State modifiers
    void setScrollX(int x) { m_state.scrollX = x; }
    void setScrollY(int y) { m_state.scrollY = y; }
    void setScroll(int x, int y) { m_state.scrollX = x; m_state.scrollY = y; }
    void setCurrentScale(int scale) { m_state.currentScale = scale; }
    void setPageDimensions(int width, int height) { m_state.pageWidth = width; m_state.pageHeight = height; }
    void setRotation(int rotation) { m_state.rotation = rotation; }
    void setMirrorH(bool mirror) { m_state.mirrorH = mirror; }
    void setMirrorV(bool mirror) { m_state.mirrorV = mirror; }
    void setTopAlignWhenFits(bool align) { m_state.topAlignWhenFits = align; }
    void setForceTopAlignNextRender(bool force) { m_state.forceTopAlignNextRender = force; }

    // Zoom operations
    void zoom(int delta, Document* document);
    void zoomTo(int scale, Document* document);
    void applyPendingZoom(Document* document, int currentPage);
    bool isZoomDebouncing() const;
    
    // Fit operations
    void fitPageToWindow(Document* document, int currentPage);
    void fitPageToWidth(Document* document, int currentPage);
    
    // Scroll operations
    void clampScroll();
    void recenterScrollOnZoom(int oldScale, int newScale);
    void alignToTopOfCurrentPage();
    
    // Page change operations
    void onPageChangedKeepZoom(Document* document, int newPage);
    void resetPageView(Document* document);
    
    // Rotation and mirroring
    void rotateClockwise();
    void toggleMirrorVertical();
    void toggleMirrorHorizontal();
    
    // Dimension helpers
    int getMaxScrollX() const;
    int getMaxScrollY() const;
    int effectiveNativeWidth(Document* document, int currentPage) const;
    int effectiveNativeHeight(Document* document, int currentPage) const;
    
    // Rendering helpers
    SDL_RendererFlip currentFlipFlags() const;
    
    // Zoom processing state
    bool isZoomProcessing() const { return m_zoomProcessing; }
    void setZoomProcessing(bool processing) { 
        m_zoomProcessing = processing; 
        if (processing) {
            m_zoomProcessingStartTime = std::chrono::steady_clock::now();
        }
    }
    bool shouldShowZoomProcessingIndicator() const;
    
    // Timing constants
#ifdef TG5040_PLATFORM
    static constexpr int ZOOM_THROTTLE_MS = 30;
    static constexpr int ZOOM_DEBOUNCE_MS = 250;
#else
    static constexpr int ZOOM_THROTTLE_MS = 25;
    static constexpr int ZOOM_DEBOUNCE_MS = 75;
#endif
    static constexpr int ZOOM_PROCESSING_MIN_DISPLAY_MS = 300;

private:
    ViewportState m_state;
    Renderer* m_renderer; // Non-owning pointer
    
    // Zoom throttling and debouncing
    std::chrono::steady_clock::time_point m_lastZoomTime;
    int m_pendingZoomDelta{0};
    std::chrono::steady_clock::time_point m_lastZoomInputTime;
    
    // Zoom processing indicator
    bool m_zoomProcessing{false};
    std::chrono::steady_clock::time_point m_zoomProcessingStartTime;
    
    // Helper methods
    void updatePageDimensions(Document* document, int currentPage);
    bool isNextRenderLikelyExpensive(int lastRenderDuration) const;
};

#endif // VIEWPORT_MANAGER_H