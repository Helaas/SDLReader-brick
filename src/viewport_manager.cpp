#include "viewport_manager.h"
#include "document.h"
#include "mupdf_document.h"
#include "renderer.h"
#include <algorithm>
#include <cmath>
#include <iostream>

ViewportManager::ViewportManager(Renderer* renderer)
    : m_renderer(renderer), m_lastZoomTime(std::chrono::steady_clock::now())
{
}

void ViewportManager::zoom(int delta, Document* document)
{
    (void) document; // Document not used after removing prerender cancellation

    // Manual zoom disables automatic fit mode
    m_state.fitMode = FitMode::None;

    // Accumulate zoom changes and track input timing for debouncing
    auto now = std::chrono::steady_clock::now();

    // Add to pending zoom delta
    m_pendingZoomDelta += delta;
    m_lastZoomInputTime = now;

    // Set zoom processing indicator
    if (!m_zoomProcessing)
    {
        m_zoomProcessing = true;
        m_zoomProcessingStartTime = now;
    }

    // If there's already a pending zoom, don't apply immediately
    // The caller will check for settled input and apply the final zoom
}

void ViewportManager::zoomTo(int scale, Document* document)
{
    (void) document; // Document not used after removing prerender cancellation

    // Manual zoom disables automatic fit mode
    m_state.fitMode = FitMode::None;

    // Always use debouncing for consistent behavior
    int targetDelta = scale - m_state.currentScale;

    // Use the zoom method with delta for consistency
    m_pendingZoomDelta = targetDelta;
    m_lastZoomInputTime = std::chrono::steady_clock::now();

    // Set zoom processing indicator
    if (!m_zoomProcessing)
    {
        m_zoomProcessing = true;
        m_zoomProcessingStartTime = m_lastZoomInputTime;
    }
}

void ViewportManager::applyPendingZoom(Document* document, int currentPage)
{
    if (m_pendingZoomDelta == 0)
    {
        return;
    }

    int oldScale = m_state.currentScale;
    int newScale = std::clamp(oldScale + m_pendingZoomDelta, 10, 350);

    if (newScale != oldScale)
    {
        // Ensure current scroll values reflect the existing page bounds
        clampScroll();

        int windowWidth = m_renderer ? m_renderer->getWindowWidth() : 0;
        int windowHeight = m_renderer ? m_renderer->getWindowHeight() : 0;

        // Store old state
        int oldScrollX = m_state.scrollX;
        int oldScrollY = m_state.scrollY;
        int oldPageWidth = m_state.pageWidth;
        int oldPageHeight = m_state.pageHeight;

        // Calculate focal point in NATIVE page coordinates (independent of downsampling)
        // This ensures the focal point is preserved even if downsampling ratios change
        int nativeWidth = effectiveNativeWidth(document, currentPage);
        int nativeHeight = effectiveNativeHeight(document, currentPage);

        // Convert current scroll position to native coordinates
        // oldPageWidth = nativeWidth * oldScale / 100 (before downsampling)
        // So native focal point = (nativeWidth / 2) - (oldScrollX * nativeWidth / oldPageWidth)
        float nativeFocalX = 0.5f;
        float nativeFocalY = 0.5f;

        if (oldPageWidth > 0 && nativeWidth > 0)
        {
            // Page pixel at viewport center in rendered coords
            float pageCenterPoint = (oldPageWidth / 2.0f) - oldScrollX;
            // Convert to native coords as a ratio
            nativeFocalX = pageCenterPoint / oldPageWidth;
        }

        if (oldPageHeight > 0 && nativeHeight > 0)
        {
            float pageCenterPoint = (oldPageHeight / 2.0f) - oldScrollY;
            nativeFocalY = pageCenterPoint / oldPageHeight;
        }

        // Apply new zoom and update dimensions (this will handle downsampling)
        m_state.currentScale = newScale;
        updatePageDimensions(document, currentPage);

        // Calculate new scroll to keep the same focal point centered
        // The focal point is at nativeFocalX * newPageWidth in the new rendered coordinates
        int newPageWidth = m_state.pageWidth;
        int newPageHeight = m_state.pageHeight;

        if (newPageWidth > windowWidth)
        {
            float focalPointInNewPage = nativeFocalX * newPageWidth;
            m_state.scrollX = static_cast<int>(std::round((newPageWidth / 2.0f) - focalPointInNewPage));
        }
        else
        {
            m_state.scrollX = 0;
        }

        if (newPageHeight > windowHeight)
        {
            float focalPointInNewPage = nativeFocalY * newPageHeight;
            m_state.scrollY = static_cast<int>(std::round((newPageHeight / 2.0f) - focalPointInNewPage));
        }
        else
        {
            m_state.scrollY = 0;
        }

        clampScroll();
    }

    // Clear pending zoom
    m_pendingZoomDelta = 0;

    // Update zoom processing state
    m_zoomProcessing = false;
}

bool ViewportManager::isZoomDebouncing() const
{
    if (m_pendingZoomDelta == 0)
    {
        return false;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - m_lastZoomInputTime)
                       .count();

    return elapsed < ZOOM_DEBOUNCE_MS;
}

void ViewportManager::fitPageToWindow(Document* document, int currentPage)
{
    if (!m_renderer)
    {
        std::cerr << "Error: ViewportManager renderer is null in fitPageToWindow" << std::endl;
        return;
    }

    // Track the fit mode for later use (e.g., on resize or rotation)
    m_state.fitMode = FitMode::FitWindow;

    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

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
    // Track the fit mode for later use (e.g., on resize or rotation)
    m_state.fitMode = FitMode::FitWidth;

    int windowWidth = m_renderer->getWindowWidth();

    // Use effective width so 90/270 rotation swaps W/H
    int nativeWidth = effectiveNativeWidth(document, currentPage);

    if (nativeWidth == 0)
    {
        std::cerr << "ViewportManager ERROR: Native page width is zero for page "
                  << currentPage << std::endl;
        return;
    }

    // Calculate scale to fit page width within window.
    // Use rounding to avoid undershooting by a pixel, which caused visible gutters.
    double scaleRatio = static_cast<double>(windowWidth) / static_cast<double>(nativeWidth);
    int scaleToFitWidth = static_cast<int>(std::lround(scaleRatio * 100.0));

    m_state.currentScale = std::clamp(scaleToFitWidth, 10, 350);

    updatePageDimensions(document, currentPage);

    m_state.scrollX = 0;
    clampScroll();
}

void ViewportManager::fitPageToHeight(Document* document, int currentPage)
{
    // Track the fit mode for later use (e.g., on resize or rotation)
    m_state.fitMode = FitMode::FitHeight;

    int windowHeight = m_renderer->getWindowHeight();

    // Use effective height so 90/270 rotation swaps W/H
    int nativeHeight = effectiveNativeHeight(document, currentPage);

    if (nativeHeight == 0)
    {
        std::cerr << "ViewportManager ERROR: Native page height is zero for page "
                  << currentPage << std::endl;
        return;
    }

    // Calculate scale to fit page height within window.
    // Use rounding to avoid undershooting by a pixel.
    double scaleRatio = static_cast<double>(windowHeight) / static_cast<double>(nativeHeight);
    int scaleToFitHeight = static_cast<int>(std::lround(scaleRatio * 100.0));

    m_state.currentScale = std::clamp(scaleToFitHeight, 10, 350);

    updatePageDimensions(document, currentPage);

    m_state.scrollY = 0;
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

void ViewportManager::recenterScrollOnZoom(int oldScrollX, int oldScrollY, int oldMaxScrollX, int oldMaxScrollY)
{
    // This function is kept for compatibility but the main zoom logic now uses
    // focal-point based zooming in applyPendingZoom for better user experience

    int newMaxScrollX = getMaxScrollX();
    if (newMaxScrollX == 0)
    {
        m_state.scrollX = 0;
    }
    else if (oldMaxScrollX > 0)
    {
        double ratioX = static_cast<double>(oldScrollX) / static_cast<double>(oldMaxScrollX);
        m_state.scrollX = static_cast<int>(std::round(ratioX * newMaxScrollX));
    }
    else
    {
        // Previously centered horizontally; remain centered after zoom
        m_state.scrollX = 0;
    }

    int newMaxScrollY = getMaxScrollY();
    if (newMaxScrollY == 0)
    {
        m_state.scrollY = 0;
    }
    else if (oldMaxScrollY > 0)
    {
        double ratioY = static_cast<double>(oldScrollY) / static_cast<double>(oldMaxScrollY);
        m_state.scrollY = static_cast<int>(std::round(ratioY * newMaxScrollY));
    }
    else
    {
        // Page previously fit vertically: respect preferred alignment when zooming in
        m_state.scrollY = m_state.topAlignWhenFits ? newMaxScrollY : 0;
    }
}

void ViewportManager::onPageChangedKeepZoom(Document* document, int newPage)
{
    // Cancel any ongoing prerendering for the old page
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
    if (muPdfDoc)
    {
        muPdfDoc->cancelPrerendering();
    }

    // Store scroll position BEFORE any updates
    int oldScrollX = m_state.scrollX;
    int oldScrollY = m_state.scrollY;

    // Recompute scaled page dims for current page (respect fit mode if active)
    if (m_state.fitMode != FitMode::None)
    {
        applyFitMode(document, newPage);
    }
    else
    {
        updatePageDimensions(document, newPage);
    }

    // ALWAYS restore scroll position - preserving panning takes priority over fit mode behavior
    // The fit mode sets the scale, but we want to keep the user's manual panning position
    m_state.scrollX = oldScrollX;
    m_state.scrollY = oldScrollY;

    // Preserve the current panning position as much as possible.
    // This keeps the viewing "window" in the same position when flipping pages,
    // which is useful when reading zoomed content (e.g., comics with margins cropped out).
    // Just clamp the scroll to the new page's valid range.
    int maxScrollX = getMaxScrollX();
    int maxScrollY = getMaxScrollY();
    m_state.scrollX = std::max(-maxScrollX, std::min(maxScrollX, m_state.scrollX));
    m_state.scrollY = std::max(-maxScrollY, std::min(maxScrollY, m_state.scrollY));

    // Don't force any specific alignment - preserve user's panning position
    m_state.forceTopAlignNextRender = false;
}

void ViewportManager::alignToTopOfCurrentPage()
{
    int windowHeight = m_renderer->getWindowHeight();

    if (m_state.pageHeight <= windowHeight && m_state.topAlignWhenFits)
    {
        // Page fits in window height, align to top
        m_state.scrollY = -(windowHeight - m_state.pageHeight) / 2;
    }
    else
    {
        // Page is taller than window, show top of page
        m_state.scrollY = getMaxScrollY();
    }

    clampScroll();
}

void ViewportManager::resetPageView(Document* document, int pageNum)
{
    // Cancel any ongoing prerendering
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
    if (muPdfDoc)
    {
        muPdfDoc->cancelPrerendering();
    }

    invalidateNativeSizeCache();

    m_state.currentScale = 100;
    m_state.rotation = 0;
    m_state.mirrorH = false;
    m_state.mirrorV = false;
    fitPageToWindow(document, pageNum);
}

void ViewportManager::rotateClockwise()
{
    m_state.rotation = (m_state.rotation + 90) % 360;

    // When rotating, swap width and height for 90/270 degree rotations
    if (m_state.rotation % 180 != 0)
    {
        std::swap(m_state.pageWidth, m_state.pageHeight);
    }

    // Reset scroll position after rotation
    m_state.scrollX = 0;
    m_state.scrollY = 0;

    // Ensure scroll remains within bounds for the new orientation
    clampScroll();
}

void ViewportManager::rotateClockwise(Document* document, int currentPage)
{
    m_state.rotation = (m_state.rotation + 90) % 360;

    // Reset scroll position after rotation
    m_state.scrollX = 0;
    m_state.scrollY = 0;

    // Clear the MuPDF cache to force re-rendering at the new (rotated) dimensions
    // This ensures the buffer is re-created immediately with the correct dimensions
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
    if (muPdfDoc)
    {
        muPdfDoc->clearCache();
    }

    // When in FitWindow mode, intelligently fit based on rotation:
    // - At 0° or 180°: fit to window normally
    // - At 90° or 270°: fit native WIDTH to window HEIGHT (since rotation swaps dimensions)
    if (m_state.fitMode == FitMode::FitWindow)
    {
        if (m_state.rotation % 180 != 0)
        {
            // Rotated 90° or 270° - the native width becomes the displayed height
            // So we fit the NATIVE width to the WINDOW height for proper scaling
            int windowHeight = m_renderer->getWindowHeight();
            auto [nativeWidth, nativeHeight] = getNativePageSize(document, currentPage);

            if (nativeWidth > 0)
            {
                double scaleRatio = static_cast<double>(windowHeight) / static_cast<double>(nativeWidth);
                int calculatedScale = static_cast<int>(std::lround(scaleRatio * 100.0));
                m_state.currentScale = std::clamp(calculatedScale, 10, 350);

                updatePageDimensions(document, currentPage);
                m_state.scrollY = 0;
                clampScroll();
            }
        }
        else
        {
            // Normal orientation - fit to window
            fitPageToWindow(document, currentPage);
        }
    }
    else
    {
        // For other fit modes, just apply the current mode
        applyFitMode(document, currentPage);
    }
}

void ViewportManager::applyFitMode(Document* document, int currentPage)
{
    switch (m_state.fitMode)
    {
    case FitMode::FitWindow:
        // When in FitWindow mode with 90°/270° rotation, fit native width to window height
        if (m_state.rotation % 180 != 0)
        {
            int windowHeight = m_renderer->getWindowHeight();
            auto [nativeWidth, nativeHeight] = getNativePageSize(document, currentPage);

            if (nativeWidth > 0)
            {
                double scaleRatio = static_cast<double>(windowHeight) / static_cast<double>(nativeWidth);
                int calculatedScale = static_cast<int>(std::lround(scaleRatio * 100.0));
                m_state.currentScale = std::clamp(calculatedScale, 10, 350);

                updatePageDimensions(document, currentPage);
                m_state.scrollY = 0;
                clampScroll();
            }
        }
        else
        {
            fitPageToWindow(document, currentPage);
        }
        break;
    case FitMode::FitWidth:
        fitPageToWidth(document, currentPage);
        break;
    case FitMode::FitHeight:
        fitPageToHeight(document, currentPage);
        break;
    case FitMode::None:
        // Manual zoom mode: just update dimensions without changing scale
        updatePageDimensions(document, currentPage);
        clampScroll();
        break;
    }
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

void ViewportManager::invalidateNativeSizeCache()
{
    m_nativeSizeCache.clear();
    m_nativeSizeCacheDoc = nullptr;
}

std::pair<int, int> ViewportManager::getNativePageSize(Document* document, int currentPage) const
{
    if (!document)
        return {0, 0};

    if (document != m_nativeSizeCacheDoc)
    {
        m_nativeSizeCacheDoc = document;
        m_nativeSizeCache.clear();
    }

    auto it = m_nativeSizeCache.find(currentPage);
    if (it != m_nativeSizeCache.end())
    {
        return it->second;
    }

    int nativeWidth = document->getPageWidthNative(currentPage);
    int nativeHeight = document->getPageHeightNative(currentPage);

    if (nativeWidth > 0 && nativeHeight > 0)
    {
        m_nativeSizeCache.emplace(currentPage, std::make_pair(nativeWidth, nativeHeight));
    }

    return {nativeWidth, nativeHeight};
}

int ViewportManager::effectiveNativeWidth(Document* document, int currentPage) const
{
    if (!document)
        return 0;

    auto [width, height] = getNativePageSize(document, currentPage);

    // Account for rotation
    if (m_state.rotation % 180 != 0)
    {
        return height; // Swapped for 90/270 degree rotation
    }
    return width;
}

int ViewportManager::effectiveNativeHeight(Document* document, int currentPage) const
{
    if (!document)
        return 0;

    auto [width, height] = getNativePageSize(document, currentPage);

    // Account for rotation
    if (m_state.rotation % 180 != 0)
    {
        return width; // Swapped for 90/270 degree rotation
    }
    return height;
}

SDL_RendererFlip ViewportManager::currentFlipFlags() const
{
    int flags = SDL_FLIP_NONE;
    if (m_state.mirrorH)
        flags |= SDL_FLIP_HORIZONTAL;
    if (m_state.mirrorV)
        flags |= SDL_FLIP_VERTICAL;
    return static_cast<SDL_RendererFlip>(flags);
}

bool ViewportManager::shouldShowZoomProcessingIndicator() const
{
    if (!m_zoomProcessing)
        return false;

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - m_zoomProcessingStartTime)
                       .count();

    return elapsed < ZOOM_PROCESSING_MIN_DISPLAY_MS || isZoomDebouncing();
}

void ViewportManager::updatePageDimensions(Document* document, int currentPage)
{
    // Update page dimensions based on effective size (accounting for downsampling)
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
    if (muPdfDoc)
    {
        if (m_renderer)
        {
            int windowWidth = m_renderer->getWindowWidth();
            int windowHeight = m_renderer->getWindowHeight();

            // Get ACTUAL native dimensions (not effective/swapped) to calculate rendered size
            // Rotation will be applied later by swapping the final dimensions
            auto [nativeWidth, nativeHeight] = getNativePageSize(document, currentPage);

            // Calculate what the page size would be at current zoom (before downsampling)
            int targetWidth = std::max(1, static_cast<int>(std::lround(nativeWidth * (m_state.currentScale / 100.0f))));
            int targetHeight = std::max(1, static_cast<int>(std::lround(nativeHeight * (m_state.currentScale / 100.0f))));

            // When rotated 90° or 270°, the effective window dimensions swap relative to the page
            // This matters for calculating headroom - we need to use the effective window dimension
            // that corresponds to each rendered dimension
            int effectiveWindowWidth = windowWidth;
            int effectiveWindowHeight = windowHeight;
            if (m_state.rotation % 180 != 0)
            {
                // At 90°/270°, swap which window dimension corresponds to which page dimension
                std::swap(effectiveWindowWidth, effectiveWindowHeight);
            }

            // Dynamically adjust max render size to ensure:
            // 1. We can actually see zoom changes (don't downsample to same size)
            // 2. We don't use excessive memory
            // 3. We respect platform constraints

            // For very low zoom levels (< 100%), we need to ensure the render buffer
            // matches the target size to avoid incorrect scaling
            int requiredWidth = targetWidth;
            int requiredHeight = targetHeight;

            // Add headroom based on zoom level to allow further zooming
            // WITHOUT changing render buffer size (which is expensive)
            // At very low zoom (< 50%), use minimal or no headroom to ensure correct initial render
            if (m_state.currentScale < 50)
            {
                // Very low zoom - no headroom to ensure correct scale rendering
                requiredWidth = targetWidth;
                requiredHeight = targetHeight;
            }
            else if (m_state.currentScale < 100)
            {
                // Low zoom (50-99%) - use 2x target to allow some zoom without rerender
                requiredWidth = std::max(targetWidth, effectiveWindowWidth * 2);
                requiredHeight = std::max(targetHeight, effectiveWindowHeight * 2);
            }
            else if (m_state.currentScale < 150)
            {
                // Moderate zoom, use 4x window size
                requiredWidth = std::max(targetWidth, effectiveWindowWidth * 4);
                requiredHeight = std::max(targetHeight, effectiveWindowHeight * 4);
            }
            else if (m_state.currentScale < 220)
            {
                // At higher zoom, use 5x
                requiredWidth = std::max(targetWidth, effectiveWindowWidth * 5);
                requiredHeight = std::max(targetHeight, effectiveWindowHeight * 5);
            }
            else
            {
                // At very high zoom, use 6x
                requiredWidth = std::max(targetWidth, effectiveWindowWidth * 6);
                requiredHeight = std::max(targetHeight, effectiveWindowHeight * 6);
            }

            // Only update maxRenderSize if it would actually change significantly
            // This avoids cache invalidation on small zoom steps
            // setMaxRenderSize internally checks if the size changed and skips if not
            muPdfDoc->setMaxRenderSize(requiredWidth, requiredHeight);

            // Query effective dimensions from MuPDF to capture exact rounding
            auto dims = muPdfDoc->getPageDimensionsEffective(currentPage, m_state.currentScale);
            if (dims.first > 0 && dims.second > 0)
            {
                m_state.pageWidth = dims.first;
                m_state.pageHeight = dims.second;
            }
            else
            {
                // Fallback to computed target dimensions if effective size unavailable
                m_state.pageWidth = targetWidth;
                m_state.pageHeight = targetHeight;
            }
        }
        else
        {
            auto dims = muPdfDoc->getPageDimensionsEffective(currentPage, m_state.currentScale);
            if (dims.first > 0 && dims.second > 0)
            {
                m_state.pageWidth = dims.first;
                m_state.pageHeight = dims.second;
            }
            else
            {
                auto [nativeWidth, nativeHeight] = getNativePageSize(document, currentPage);
                m_state.pageWidth = std::max(1, static_cast<int>(std::lround(nativeWidth * (m_state.currentScale / 100.0f))));
                m_state.pageHeight = std::max(1, static_cast<int>(std::lround(nativeHeight * (m_state.currentScale / 100.0f))));
            }
        }

        // Apply rotation
        if (m_state.rotation % 180 != 0)
        {
            std::swap(m_state.pageWidth, m_state.pageHeight);
        }
    }
    else
    {
        // Fallback for other document types
        auto [nativeWidth, nativeHeight] = getNativePageSize(document, currentPage);
        m_state.pageWidth = std::max(1, static_cast<int>(std::lround(nativeWidth * (m_state.currentScale / 100.0))));
        m_state.pageHeight = std::max(1, static_cast<int>(std::lround(nativeHeight * (m_state.currentScale / 100.0))));

        // Apply rotation swap
        if (m_state.rotation % 180 != 0)
        {
            std::swap(m_state.pageWidth, m_state.pageHeight);
        }
    }
}

bool ViewportManager::isNextRenderLikelyExpensive(int lastRenderDuration) const
{
    static constexpr int EXPENSIVE_RENDER_THRESHOLD_MS = 200;
    return lastRenderDuration > EXPENSIVE_RENDER_THRESHOLD_MS;
}
