#include "app.h"
#include "renderer.h"
#include "text_renderer.h"
#include "document.h"
#include "pdf_document.h"
#include "djvu_document.h"

#include <iostream>
#include <stdexcept>
#include <algorithm>

// --- App Class ---


App::App(const std::string& filename, int initialWidth, int initialHeight)
    : m_running(true), m_currentPage(0), m_currentScale(100),
      m_scrollX(0), m_scrollY(0), m_pageWidth(0), m_pageHeight(0),
      m_pageWidthNative(0), m_pageHeightNative(0), // Initialize new members
      m_isDragging(false), m_lastTouchX(0.0f), m_lastTouchY(0.0f),
      m_gameController(nullptr), m_gameControllerInstanceID(-1) {

    m_renderer = std::make_unique<Renderer>(initialWidth, initialHeight, "SDLBook C++");

    m_textRenderer = std::make_unique<TextRenderer>(m_renderer->getSDLRenderer(), "res/Roboto-Regular.ttf", 16);

    if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".pdf") {
        m_document = std::make_unique<PdfDocument>();
    } else if (filename.length() >= 5 && filename.substr(filename.length() - 5) == ".djvu") {
        m_document = std::make_unique<DjvuDocument>();
    } else {
        throw std::runtime_error("Unsupported file format. Only .pdf and .djvu are supported.");
    }

    if (!m_document->open(filename)) {
        throw std::runtime_error("Failed to open document: " + filename);
    }
    m_pageCount = m_document->getPageCount();

    changePage(0); 
    fitPageToWindow();

    initializeGameControllers();
}

void App::run() {
    Uint32 lastRenderTime = SDL_GetTicks();
    while (m_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            handleEvent(event);
        }

        Uint32 currentTime = SDL_GetTicks();
        if (currentTime - lastRenderTime > 16) { // Cap at ~60 FPS
            update();
            render();
            lastRenderTime = currentTime;
        }
    }
    closeGameControllers();
}

void App::handleEvent(const SDL_Event& event) {
    AppAction action = AppAction::None;
    int deltaValue = 0; // For scroll/zoom changes

    switch (event.type) {
        case SDL_QUIT:
            action = AppAction::Quit;
            break;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                fitPageToWindow(); 
            }
            break;

        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    action = AppAction::Quit;
                    break;
                case SDLK_RIGHT:
                    action = AppAction::PageNext;
                    break;
                case SDLK_LEFT:
                    action = AppAction::PagePrevious;
                    break;
                case SDLK_UP:
                    action = AppAction::ScrollUp;
                    deltaValue = 30; // Scroll amount
                    break;
                case SDLK_DOWN:
                    action = AppAction::ScrollDown;
                    deltaValue = 30; // Scroll amount
                    break;
                case SDLK_KP_PLUS:
                case SDLK_PLUS:
                    action = AppAction::ZoomIn;
                    deltaValue = 5; // Zoom percentage
                    break;
                case SDLK_KP_MINUS:
                case SDLK_MINUS:
                    action = AppAction::ZoomOut;
                    deltaValue = 5; // Zoom percentage
                    break;
                case SDLK_f:
                    action = AppAction::ToggleFullscreen;
                    break;
                case SDLK_HOME: // Jump to first page
                    action = AppAction::None; // Handled directly
                    jumpToPage(0);
                    break;
                case SDLK_END: // Jump to last page
                    action = AppAction::None; // Handled directly
                    jumpToPage(m_pageCount - 1);
                    break;
                // Add more key bindings as needed
            }
            break;

        case SDL_MOUSEWHEEL:
            action = mapMouseWheelToAppAction(event.wheel.y, SDL_GetModState(), deltaValue);
            break;

        case SDL_CONTROLLERBUTTONDOWN:
            switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    action = AppAction::PageNext;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    action = AppAction::PagePrevious;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    action = AppAction::ScrollUp;
                    deltaValue = 30;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    action = AppAction::ScrollDown;
                    deltaValue = 30;
                    break;
                case SDL_CONTROLLER_BUTTON_A: // Example: 'A' button for ZoomIn
                    action = AppAction::ZoomIn;
                    deltaValue = 5;
                    break;
                case SDL_CONTROLLER_BUTTON_B: // Example: 'B' button for ZoomOut
                    action = AppAction::ZoomOut;
                    deltaValue = 5;
                    break;
                case SDL_CONTROLLER_BUTTON_START:
                    action = AppAction::Quit;
                    break;
            }
            break;

        case SDL_CONTROLLERAXISMOTION:
            // Assuming left stick for scrolling
            if (event.caxis.which == m_gameControllerInstanceID) {
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                    action = mapControllerAxisToAppAction(SDL_CONTROLLER_AXIS_LEFTX, event.caxis.value, deltaValue);
                } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    action = mapControllerAxisToAppAction(SDL_CONTROLLER_AXIS_LEFTY, event.caxis.value, deltaValue);
                }
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_isDragging = true;
                m_lastTouchX = static_cast<float>(event.button.x);
                m_lastTouchY = static_cast<float>(event.button.y);
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_isDragging = false;
            }
            break;

        case SDL_MOUSEMOTION:
            if (m_isDragging) {
                float currentTouchX = static_cast<float>(event.motion.x);
                float currentTouchY = static_cast<float>(event.motion.y);
                float deltaX = currentTouchX - m_lastTouchX;
                float deltaY = currentTouchY - m_lastTouchY;

                changeScroll(static_cast<int>(deltaX), static_cast<int>(deltaY));

                m_lastTouchX = currentTouchX;
                m_lastTouchY = currentTouchY;
            }
            break;

        // Touch events (for mobile/tablet support)
        case SDL_FINGERDOWN:
            if (event.tfinger.fingerId == 0) { // Only track the first finger
                m_isDragging = true;
                m_lastTouchX = event.tfinger.x * m_renderer->getWindowWidth();
                m_lastTouchY = event.tfinger.y * m_renderer->getWindowHeight();
            }
            break;
        case SDL_FINGERUP:
            if (event.tfinger.fingerId == 0) {
                m_isDragging = false;
            }
            break;
        case SDL_FINGERMOTION:
            if (m_isDragging && event.tfinger.fingerId == 0) {
                float currentTouchX = event.tfinger.x * m_renderer->getWindowWidth();
                float currentTouchY = event.tfinger.y * m_renderer->getWindowHeight();
                float deltaX = currentTouchX - m_lastTouchX;
                float deltaY = currentTouchY - m_lastTouchY;

                changeScroll(static_cast<int>(deltaX), static_cast<int>(deltaY));

                m_lastTouchX = currentTouchX;
                m_lastTouchY = currentTouchY;
            }
            break;
    }
    processAppAction(action, deltaValue);
}

App::AppAction App::mapMouseWheelToAppAction(int y_delta, SDL_Keymod mod, int& deltaValue) {
    if (mod & KMOD_CTRL) { // Ctrl + scroll for zoom
        if (y_delta > 0) {
            deltaValue = 5;
            return AppAction::ZoomIn;
        } else if (y_delta < 0) {
            deltaValue = 5;
            return AppAction::ZoomOut;
        }
    } else { // Regular scroll for vertical scrolling
        if (y_delta > 0) {
            deltaValue = 30;
            return AppAction::ScrollUp;
        } else if (y_delta < 0) {
            deltaValue = 30;
            return AppAction::ScrollDown;
        }
    }
    return AppAction::None;
}

App::AppAction App::mapControllerAxisToAppAction(SDL_GameControllerAxis axis, Sint16 value, int& deltaValue) {
    const int AXIS_DEAD_ZONE = 8000; // Dead zone to prevent accidental input

    if (value < -AXIS_DEAD_ZONE) {
        deltaValue = (abs(value) - AXIS_DEAD_ZONE) / 100; // Scale sensitivity
        if (axis == SDL_CONTROLLER_AXIS_LEFTX) return AppAction::ScrollLeft;
        if (axis == SDL_CONTROLLER_AXIS_LEFTY) return AppAction::ScrollUp;
    } else if (value > AXIS_DEAD_ZONE) {
        deltaValue = (value - AXIS_DEAD_ZONE) / 100; // Scale sensitivity
        if (axis == SDL_CONTROLLER_AXIS_LEFTX) return AppAction::ScrollRight;
        if (axis == SDL_CONTROLLER_AXIS_LEFTY) return AppAction::ScrollDown;
    }
    return AppAction::None;
}

bool App::processAppAction(AppAction action, int deltaValue) {
    switch (action) {
        case AppAction::Quit:
            m_running = false;
            return true;
        case AppAction::PageNext:
            return changePage(1);
        case AppAction::PagePrevious:
            return changePage(-1);
        case AppAction::ScrollUp:
            return changeScroll(0, deltaValue);
        case AppAction::ScrollDown:
            return changeScroll(0, -deltaValue);
        case AppAction::ScrollLeft:
            return changeScroll(deltaValue, 0);
        case AppAction::ScrollRight:
            return changeScroll(-deltaValue, 0);
        case AppAction::ZoomIn:
            return changeScale(deltaValue);
        case AppAction::ZoomOut:
            return changeScale(-deltaValue);
        case AppAction::ToggleFullscreen:
            m_renderer->toggleFullscreen();
            // After toggling fullscreen, the window size might change,
            // so refit the page to the new window dimensions.
            fitPageToWindow(); // NEW: Call to fit page to new fullscreen/windowed size
            return true;
        default:
            return false;
    }
}

bool App::changePage(int delta) {
    int newPage = m_currentPage + delta;
    if (newPage >= 0 && newPage < m_pageCount) {
        m_currentPage = newPage;
        // When changing page, always get native dimensions first
        m_pageWidthNative = m_document->getPageWidthNative(m_currentPage);
        m_pageHeightNative = m_document->getPageHeightNative(m_currentPage);

        // Recalculate scaled dimensions based on current scale
        // These are *expected* scaled dimensions based on m_currentScale.
        // The actual rendered dimensions might slightly differ and will be updated in renderCurrentPage.
        m_pageWidth = static_cast<int>(m_pageWidthNative * (static_cast<double>(m_currentScale) / 100.0));
        m_pageHeight = static_cast<int>(m_pageHeightNative * (static_cast<double>(m_currentScale) / 100.0));

        // Reset scroll position to top-left for new page
        m_scrollX = 0;
        m_scrollY = 0;
        return true;
    }
    return false;
}

bool App::jumpToPage(int pageNum) {
    if (pageNum >= 0 && pageNum < m_pageCount) {
        m_currentPage = pageNum;
        // When changing page, always get native dimensions first
        m_pageWidthNative = m_document->getPageWidthNative(m_currentPage);
        m_pageHeightNative = m_document->getPageHeightNative(m_currentPage);

        // Recalculate scaled dimensions based on current scale
        // These are *expected* scaled dimensions. Actual rendered dimensions will be updated in renderCurrentPage.
        m_pageWidth = static_cast<int>(m_pageWidthNative * (static_cast<double>(m_currentScale) / 100.0));
        m_pageHeight = static_cast<int>(m_pageHeightNative * (static_cast<double>(m_currentScale) / 100.0));

        // Reset scroll position to top-left for new page
        m_scrollX = 0;
        m_scrollY = 0;
        return true;
    }
    return false;
}


bool App::changeScale(int delta) {
    int newScale = m_currentScale + delta;
    // Clamp scale to reasonable bounds (e.g., 10% to 1000%)
    if (newScale < 10) newScale = 10;
    if (newScale > 1000) newScale = 1000;

    if (newScale != m_currentScale) {
        // Store current scroll position relative to page content
        double oldPageWidth = m_pageWidth;
        double oldPageHeight = m_pageHeight;

        m_currentScale = newScale;

        // Recalculate page dimensions based on native size and new scale
        // These are *expected* scaled dimensions. Actual rendered dimensions will be updated in renderCurrentPage.
        m_pageWidth = static_cast<int>(m_pageWidthNative * (static_cast<double>(m_currentScale) / 100.0));
        m_pageHeight = static_cast<int>(m_pageHeightNative * (static_cast<double>(m_currentScale) / 100.0));

        // Adjust scroll position to keep the center of the view somewhat stable
        if (oldPageWidth > 0 && oldPageHeight > 0) {
            double ratioX = static_cast<double>(m_scrollX + m_renderer->getWindowWidth() / 2) / oldPageWidth;
            double ratioY = static_cast<double>(m_scrollY + m_renderer->getWindowHeight() / 2) / oldPageHeight;

            m_scrollX = static_cast<int>(ratioX * m_pageWidth - m_renderer->getWindowWidth() / 2);
            m_scrollY = static_cast<int>(ratioY * m_pageHeight - m_renderer->getWindowHeight() / 2);
        }

        // Clamp scroll after adjustment
        changeScroll(0,0); 

        return true;
    }
    return false;
}

bool App::changeScroll(int deltaX, int deltaY) {
    int newScrollX = m_scrollX + deltaX;
    int newScrollY = m_scrollY + deltaY;

    // Clamp scroll to ensure content stays within bounds.
    // Max scroll is (page_dimension - window_dimension)
    int maxScrollX = std::max(0, m_pageWidth - m_renderer->getWindowWidth());
    int maxScrollY = std::max(0, m_pageHeight - m_renderer->getWindowHeight());

    m_scrollX = std::min(std::max(0, newScrollX), maxScrollX);
    m_scrollY = std::min(std::max(0, newScrollY), maxScrollY);

    return true;
}

void App::update() {
    // progress bars or animation updates can go here
}

void App::renderCurrentPage() {
    if (!m_document) return;

    int actualRenderedWidth = 0; 
    int actualRenderedHeight = 0; 

    // Call document->renderPage, which will fill actualRenderedWidth/Height with the true dimensions of the rendered image
    std::vector<uint8_t> pixelData = m_document->renderPage(m_currentPage, actualRenderedWidth, actualRenderedHeight, m_currentScale);

    if (pixelData.empty() || actualRenderedWidth == 0 || actualRenderedHeight == 0) {
        std::cerr << "App ERROR: Failed to get pixel data or invalid dimensions from document->renderPage." << std::endl;
        return;
    }

    // Crucially: Update m_pageWidth/m_pageHeight to the ACTUAL rendered dimensions.
    // This ensures consistency for scrolling, UI overlay, and drawing.
    if (actualRenderedWidth != m_pageWidth || actualRenderedHeight != m_pageHeight) {
        std::cerr << "App WARNING: Document rendered dimensions (" << actualRenderedWidth << "x" << actualRenderedHeight
                  << ") mismatch App's *expected* scaled dimensions (" << m_pageWidth << "x" << m_pageHeight << ")." << std::endl;
        m_pageWidth = actualRenderedWidth;
        m_pageHeight = actualRenderedHeight;
        // If a mismatch happens, re-clamp scroll immediately to reflect new page size limits
        changeScroll(0,0);
    }

    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

    // Calculate the drawing position (destX, destY) for the rendered page on the window.
    // If the page is smaller than the window in a dimension, center it.
    // Otherwise, apply the scroll offset (which can be 0 if the page fits).
    int drawX = -m_scrollX;
    int drawY = -m_scrollY;

    // Center horizontally if the scaled page is narrower than the window
    if (m_pageWidth < windowWidth) {
        drawX = (windowWidth - m_pageWidth) / 2;
    }
    // Center vertically if the scaled page is shorter than the window
    if (m_pageHeight < windowHeight) {
        drawY = (windowHeight - m_pageHeight) / 2;
    }

    // Pass the actual rendered dimensions to the renderer for texture creation (srcWidth/srcHeight),
    // and the calculated drawX, drawY, and actual rendered dimensions for the destination rect (destWidth/destHeight).
    m_renderer->renderPage(pixelData, actualRenderedWidth, actualRenderedHeight,
                           drawX, drawY, actualRenderedWidth, actualRenderedHeight);

}

void App::renderUIOverlay() {
    int currentWindowWidth = m_renderer->getWindowWidth();
    int currentWindowHeight = m_renderer->getWindowHeight();

    // Ensure the font size is updated based on the current scale before rendering text.
    // Consider scaling the font size more reasonably, e.g., for 100% zoom, a default font size.
    // The current approach `m_currentScale` directly will make the font huge at high zooms.
    // Let's set a base font size and apply a smaller scaling factor or a fixed size.
    int baseFontSize = 16;
    m_textRenderer->setFontSize(baseFontSize); // Keep font size consistent regardless of document zoom

    SDL_Color textColor = { 0, 0, 0, 255 };
    std::string pageInfo = "Page: " + std::to_string(m_currentPage + 1) + "/" + std::to_string(m_pageCount);
    std::string scaleInfo = "Scale: " + std::to_string(m_currentScale) + "%";

    // Render page information at the bottom center of the window.
    // The text width estimation (length * 8) is a simple heuristic.
    m_textRenderer->renderText(pageInfo,
                               (currentWindowWidth - static_cast<int>(pageInfo.length()) * 8) / 2,
                               currentWindowHeight - 30, textColor);

    // Render scale information at the top right of the window.
    m_textRenderer->renderText(scaleInfo,
                               currentWindowWidth - static_cast<int>(scaleInfo.length()) * 8 - 10,
                               10, textColor);
}

void App::render() {
    // CORRECTED: Provide RGBA values for clear
    m_renderer->clear(255, 255, 255, 255); // Clear to white background
    renderCurrentPage();
    renderUIOverlay();
    m_renderer->present();
}

// NEW: fitPageToWindow implementation
void App::fitPageToWindow() {
    if (!m_document || m_pageWidthNative == 0 || m_pageHeightNative == 0) {
        std::cerr << "App WARNING: fitPageToWindow called but document not loaded or native dimensions invalid." << std::endl;
        return; // Document not loaded or native dimensions not available yet
    }

    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

    if (windowWidth <= 0 || windowHeight <= 0) {
        std::cerr << "App WARNING: fitPageToWindow called but window dimensions are invalid: " << windowWidth << "x" << windowHeight << std::endl;
        return;
    }

    double scaleX = static_cast<double>(windowWidth) / m_pageWidthNative;
    double scaleY = static_cast<double>(windowHeight) / m_pageHeightNative;

    double fitScale = std::min(scaleX, scaleY);

    // Convert to percentage and clamp to reasonable bounds
    int newScalePercentage = static_cast<int>(fitScale * 100);
    newScalePercentage = std::max(10, newScalePercentage);   // Minimum 10%
    newScalePercentage = std::min(1000, newScalePercentage); // Maximum 1000%

    // Only update if scale actually changes significantly
    if (newScalePercentage != m_currentScale) {
        m_currentScale = newScalePercentage;

        // Recalculate page dimensions based on native size and new "fit" scale.
        // These are *expected* scaled dimensions. Actual rendered dimensions will be updated in renderCurrentPage.
        m_pageWidth = static_cast<int>(m_pageWidthNative * (static_cast<double>(m_currentScale) / 100.0));
        m_pageHeight = static_cast<int>(m_pageHeightNative * (static_cast<double>(m_currentScale) / 100.0));
    }

    // For fit-to-window, the scroll position should be reset to (0,0) (top-left of page content)
    // The centering on screen will be handled by renderCurrentPage.
    m_scrollX = 0;
    m_scrollY = 0;
    // Ensure scroll is clamped correctly, which for 0,0 will just ensure it's within limits (which it should be).
    changeScroll(0,0);

}


void App::initializeGameControllers() {
    // Check for game controllers
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            m_gameController = SDL_GameControllerOpen(i);
            if (m_gameController) {
                std::cout << "Found game controller: " << SDL_GameControllerName(m_gameController) << std::endl;
                m_gameControllerInstanceID = SDL_JoystickGetDeviceInstanceID(i);
                break; // Use the first one found
            }
        }
    }
}

void App::closeGameControllers() {
    if (m_gameController) {
        SDL_GameControllerClose(m_gameController);
        m_gameController = nullptr;
        m_gameControllerInstanceID = -1;
    }
}

void App::logCurrentState() {
    std::cout << "--- App State ---" << std::endl;
    std::cout << "Current Page: " << m_currentPage + 1 << "/" << m_pageCount << std::endl;
    std::cout << "Native Page Dimensions: " << m_pageWidthNative << "x" << m_pageHeightNative << std::endl;
    std::cout << "Current Scale: " << m_currentScale << "%" << std::endl;
    std::cout << "Scaled Page Dimensions: " << m_pageWidth << "x" << m_pageHeight << " (Expected/Actual)" << std::endl;
    std::cout << "Scroll Position (Page Offset): X=" << m_scrollX << ", Y=" << m_scrollY << std::endl;
    std::cout << "Window Dimensions: " << m_renderer->getWindowWidth() << "x" << m_renderer->getWindowHeight() << std::endl;
    std::cout << "-----------------" << std::endl;
}