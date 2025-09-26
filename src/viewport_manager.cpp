#include "viewport_manager.h"
#include "renderer.h"
#include "document.h"
#include "mupdf_document.h"
#include <algorithm>
#include <iostream>

ViewportManager::ViewportManager(Renderer* renderer)
    : m_renderer(renderer), m_lastZoomTime(std::chrono::steady_clock::now())
{
}

void ViewportManager::zoom(int delta, Document* document)
{
    // Accumulate zoom changes and track input timing for debouncing
    auto now = std::chrono::steady_clock::now();
    
    // Cancel any ongoing prerendering since zoom is changing
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
    if (muPdfDoc) {
        muPdfDoc->cancelPrerendering();
    }
    
    // Add to pending zoom delta
    m_pendingZoomDelta += delta;
    m_lastZoomInputTime = now;
    
    // Set zoom processing indicator
    if (!m_zoomProcessing) {
        m_zoomProcessing = true;
        m_zoomProcessingStartTime = now;
    }
    
    // If there's already a pending zoom, don't apply immediately
    // The caller will check for settled input and apply the final zoom
}

void ViewportManager::zoomTo(int scale, Document* document)
{
    // Always use debouncing for consistent behavior
    int targetDelta = scale - m_state.currentScale;
    
    // Cancel any ongoing prerendering since zoom is changing
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
    if (muPdfDoc) {
        muPdfDoc->cancelPrerendering();
    }
    
    // Use the zoom method with delta for consistency
    m_pendingZoomDelta = targetDelta;
    m_lastZoomInputTime = std::chrono::steady_clock::now();
    
    // Set zoom processing indicator
    if (!m_zoomProcessing) {
        m_zoomProcessing = true;
        m_zoomProcessingStartTime = m_lastZoomInputTime;
    }
}

void ViewportManager::applyPendingZoom(Document* document, int currentPage)
{
    if (m_pendingZoomDelta == 0) {
        return;
    }
    
    int oldScale = m_state.currentScale;
    int newScale = std::clamp(oldScale + m_pendingZoomDelta, 10, 350);
    
    if (newScale != oldScale) {
        m_state.currentScale = newScale;
        updatePageDimensions(document, currentPage);
        recenterScrollOnZoom(oldScale, newScale);
        clampScroll();
    }
    
    // Clear pending zoom
    m_pendingZoomDelta = 0;
    
    // Update zoom processing state
    m_zoomProcessing = false;
}

bool ViewportManager::isZoomDebouncing() const
{
    if (m_pendingZoomDelta == 0) {
        return false;
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_lastZoomInputTime).count();
    
    return elapsed < ZOOM_DEBOUNCE_MS;
}

void ViewportManager::fitPageToWindow(Document* document, int currentPage)
{
    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

#ifndef TG5040_PLATFORM
    // Update max render size for downsampling - allow for meaningful zoom levels on non-TG5040 platforms
    // Use 4x window size to enable proper zooming while TG5040 has no limit
    if (auto muDoc = dynamic_cast<MuPdfDocument*>(document))
    {
        // Allow 4x zoom by setting max render size to 4x window size
        muDoc->setMaxRenderSize(windowWidth * 4, windowHeight * 4);
    }
#endif

    // Use effective sizes so 90/270 rotation swaps W/H
    int nativeWidth = effectiveNativeWidth(document, currentPage);
    int nativeHeight = effectiveNativeHeight(document, currentPage);

    if (nativeWidth == 0 || nativeHeight == 0)
    {
        std::cerr << "ViewportManager ERROR: Native page dimensions are zero for page "
                  << currentPage << std::endl;
        return;
    }

    int scaleToFitWidth = static_cast<int>((static_cast<double>(windowWidth) / nativeWidth) * 100.0);
    int scaleToFitHeight = static_cast<int>((static_cast<double>(windowHeight) / nativeHeight) * 100.0);

    m_state.currentScale = std::min(scaleToFitWidth, scaleToFitHeight);
    if (m_state.currentScale < 10)
        m_state.currentScale = 10;
    if (m_state.currentScale > 350)
        m_state.currentScale = 350;

    updatePageDimensions(document, currentPage);

    m_state.scrollX = 0;
    m_state.scrollY = 0;
    
    clampScroll();
}

void ViewportManager::fitPageToWidth(Document* document, int currentPage)
{
    int windowWidth = m_renderer->getWindowWidth();

#ifndef TG5040_PLATFORM
    // Update max render size for downsampling
    if (auto muDoc = dynamic_cast<MuPdfDocument*>(document))
    {
        int windowHeight = m_renderer->getWindowHeight();
        muDoc->setMaxRenderSize(windowWidth * 4, windowHeight * 4);
    }
#endif

    // Use effective width so 90/270 rotation swaps W/H
    int nativeWidth = effectiveNativeWidth(document, currentPage);

    if (nativeWidth == 0)
    {
        std::cerr << "ViewportManager ERROR: Native page width is zero for page "
                  << currentPage << std::endl;
        return;
    }

    int scaleToFitWidth = static_cast<int>((static_cast<double>(windowWidth) / nativeWidth) * 100.0);

    m_state.currentScale = std::clamp(scaleToFitWidth, 10, 350);

    updatePageDimensions(document, currentPage);

    m_state.scrollX = 0;
    clampScroll();
}

void ViewportManager::clampScroll()
{
    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

    int maxScrollX = std::max(0, (m_state.pageWidth - windowWidth) / 2);
    int maxScrollY = std::max(0, (m_state.pageHeight - windowHeight) / 2);

    m_state.scrollX = std::max(-maxScrollX, std::min(maxScrollX, m_state.scrollX));
    m_state.scrollY = std::max(-maxScrollY, std::min(maxScrollY, m_state.scrollY));
}

void ViewportManager::recenterScrollOnZoom(int oldScale, int newScale)
{
    if (oldScale == 0) return; // Prevent division by zero

    // Calculate the scaling factor
    double scaleFactor = static_cast<double>(newScale) / oldScale;

    // Scale the current scroll position
    m_state.scrollX = static_cast<int>(m_state.scrollX * scaleFactor);
    m_state.scrollY = static_cast<int>(m_state.scrollY * scaleFactor);
}

void ViewportManager::onPageChangedKeepZoom(Document* document, int newPage)
{
    // Cancel any ongoing prerendering for the old page
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
    if (muPdfDoc) {
        muPdfDoc->cancelPrerendering();
    }

    // Recompute scaled page dims for current page at the existing zoom
    updatePageDimensions(document, newPage);
    
    // Set the flag to force top alignment on next render
    m_state.forceTopAlignNextRender = true;
    
    // Clamp scroll to new page dimensions
    clampScroll();
}

void ViewportManager::alignToTopOfCurrentPage()
{
    int windowHeight = m_renderer->getWindowHeight();
    
    if (m_state.pageHeight <= windowHeight && m_state.topAlignWhenFits) {
        // Page fits in window height, align to top
        m_state.scrollY = -(windowHeight - m_state.pageHeight) / 2;
    } else {
        // Page is taller than window, show top of page
        m_state.scrollY = getMaxScrollY();
    }
    
    clampScroll();
}

void ViewportManager::resetPageView(Document* document)
{
    m_state.currentScale = 100;
    m_state.rotation = 0;
    m_state.mirrorH = false;
    m_state.mirrorV = false;
    fitPageToWindow(document, 0); // Assuming page 0 for reset
}

void ViewportManager::rotateClockwise()
{
    m_state.rotation = (m_state.rotation + 90) % 360;
    
    // When rotating, swap width and height for 90/270 degree rotations
    if (m_state.rotation % 180 != 0) {
        std::swap(m_state.pageWidth, m_state.pageHeight);
    }
    
    // Reset scroll position after rotation
    m_state.scrollX = 0;
    m_state.scrollY = 0;
}

void ViewportManager::toggleMirrorVertical()
{
    m_state.mirrorV = !m_state.mirrorV;
}

void ViewportManager::toggleMirrorHorizontal()
{
    m_state.mirrorH = !m_state.mirrorH;
}

int ViewportManager::getMaxScrollX() const
{
    int windowWidth = m_renderer->getWindowWidth();
    return std::max(0, (m_state.pageWidth - windowWidth) / 2);
}

int ViewportManager::getMaxScrollY() const
{
    int windowHeight = m_renderer->getWindowHeight();
    return std::max(0, (m_state.pageHeight - windowHeight) / 2);
}

int ViewportManager::effectiveNativeWidth(Document* document, int currentPage) const
{
    if (!document) return 0;
    
    int width = document->getPageWidthNative(currentPage);
    int height = document->getPageHeightNative(currentPage);
    
    // Account for rotation
    if (m_state.rotation % 180 != 0) {
        return height; // Swapped for 90/270 degree rotation
    }
    return width;
}

int ViewportManager::effectiveNativeHeight(Document* document, int currentPage) const
{
    if (!document) return 0;
    
    int width = document->getPageWidthNative(currentPage);
    int height = document->getPageHeightNative(currentPage);
    
    // Account for rotation
    if (m_state.rotation % 180 != 0) {
        return width; // Swapped for 90/270 degree rotation
    }
    return height;
}

SDL_RendererFlip ViewportManager::currentFlipFlags() const
{
    int flags = SDL_FLIP_NONE;
    if (m_state.mirrorH) flags |= SDL_FLIP_HORIZONTAL;
    if (m_state.mirrorV) flags |= SDL_FLIP_VERTICAL;
    return static_cast<SDL_RendererFlip>(flags);
}

bool ViewportManager::shouldShowZoomProcessingIndicator() const
{
    if (!m_zoomProcessing) return false;
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_zoomProcessingStartTime).count();
    
    return elapsed < ZOOM_PROCESSING_MIN_DISPLAY_MS || isZoomDebouncing();
}

void ViewportManager::updatePageDimensions(Document* document, int currentPage)
{
    // Update page dimensions based on effective size (accounting for downsampling)
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
    if (muPdfDoc) {
        m_state.pageWidth = muPdfDoc->getPageWidthEffective(currentPage, m_state.currentScale);
        m_state.pageHeight = muPdfDoc->getPageHeightEffective(currentPage, m_state.currentScale);
        
        // Apply rotation
        if (m_state.rotation % 180 != 0) {
            std::swap(m_state.pageWidth, m_state.pageHeight);
        }
    } else {
        // Fallback for other document types
        int nativeWidth = effectiveNativeWidth(document, currentPage);
        int nativeHeight = effectiveNativeHeight(document, currentPage);
        m_state.pageWidth = static_cast<int>(nativeWidth * (m_state.currentScale / 100.0));
        m_state.pageHeight = static_cast<int>(nativeHeight * (m_state.currentScale / 100.0));
    }
}

bool ViewportManager::isNextRenderLikelyExpensive(int lastRenderDuration) const
{
    static constexpr int EXPENSIVE_RENDER_THRESHOLD_MS = 200;
    return lastRenderDuration > EXPENSIVE_RENDER_THRESHOLD_MS;
}