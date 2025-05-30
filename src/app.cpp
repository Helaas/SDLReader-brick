#include "app.h"
#include "renderer.h"
#include "text_renderer.h"
#include "document.h"
#include "pdf_document.h"
// #include "djvu_document.h" // Removed for DjVu support removal

#include <iostream>
#include <stdexcept>
#include <algorithm>

// --- App Class ---

// Constructor now accepts pre-initialized SDL_Window* and SDL_Renderer*
App::App(const std::string& filename, SDL_Window* window, SDL_Renderer* renderer)
    : m_running(true), m_currentPage(0), m_currentScale(100),
      m_scrollX(0), m_scrollY(0), m_pageWidth(0), m_pageHeight(0),
      m_isDragging(false), m_lastTouchX(0.0f), m_lastTouchY(0.0f),
      m_gameController(nullptr), m_gameControllerInstanceID(-1) {

    // Pass the pre-initialized window and renderer to the Renderer object
    m_renderer = std::make_unique<Renderer>(window, renderer);

    m_textRenderer = std::make_unique<TextRenderer>(m_renderer->getSDLRenderer(), "res/Roboto-Regular.ttf", 16);

    if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".pdf") {
        m_document = std::make_unique<PdfDocument>();
    } else {
        throw std::runtime_error("Unsupported file format: " + filename);
    }

    if (!m_document->open(filename)) {
        throw std::runtime_error("Failed to open document: " + filename);
    }

    m_pageCount = m_document->getPageCount();
    if (m_pageCount == 0) {
        throw std::runtime_error("Document contains no pages: " + filename);
    }

    // Initial page load and fit
    loadDocument();

    // Initialize game controllers
    initializeGameControllers();
}

App::~App() {
    closeGameControllers();
    // SDL_Quit() and TTF_Quit() are now handled in main.cpp
}

void App::run() {
    SDL_Event event;
    while (m_running) {
        // Event handling
        while (SDL_PollEvent(&event) != 0) {
            handleEvent(event);
        }

        // Rendering
        renderCurrentPage();
        renderUI();
        m_renderer->present();
    }
}

void App::handleEvent(const SDL_Event& event) {
    AppAction action = AppAction::None;

    switch (event.type) {
        case SDL_QUIT:
            action = AppAction::Quit;
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                action = AppAction::Resize;
            }
            break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    action = AppAction::Quit;
                    break;
                case SDLK_RIGHT:
                    goToNextPage();
                    break;
                case SDLK_LEFT:
                    goToPreviousPage();
                    break;
                case SDLK_HOME:
                    goToPage(0);
                    break;
                case SDLK_END:
                    goToPage(m_pageCount - 1);
                    break;
                case SDLK_UP:
                    zoom(10);
                    break;
                case SDLK_DOWN:
                    zoom(-10);
                    break;
                case SDLK_0:
                    zoomTo(100);
                    break;
                case SDLK_f:
                    m_renderer->toggleFullscreen();
                    fitPageToWindow();
                    break;
                case SDLK_p:
                    printAppState();
                    break;
                case SDLK_r:
                    resetPageView();
                    break;
                case SDLK_c:
                    clampScroll();
                    break;
            }
            break;
        case SDL_MOUSEWHEEL:
            if (event.wheel.y > 0) {
                if (SDL_GetModState() & KMOD_CTRL) {
                    zoom(10);
                } else {
                    m_scrollY += 50;
                }
            } else if (event.wheel.y < 0) {
                if (SDL_GetModState() & KMOD_CTRL) {
                    zoom(-10);
                } else {
                    m_scrollY -= 50;
                }
            }
            clampScroll();
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
                float dx = static_cast<float>(event.motion.x) - m_lastTouchX;
                float dy = static_cast<float>(event.motion.y) - m_lastTouchY;
                m_scrollX += static_cast<int>(dx);
                m_scrollY += static_cast<int>(dy);
                m_lastTouchX = static_cast<float>(event.motion.x);
                m_lastTouchY = static_cast<float>(event.motion.y);
                clampScroll();
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (event.caxis.which == m_gameControllerInstanceID) {
                const Sint16 AXIS_DEAD_ZONE = 8000;

                switch (event.caxis.axis) {
                    case SDL_CONTROLLER_AXIS_LEFTX:
                    case SDL_CONTROLLER_AXIS_RIGHTX:
                        if (event.caxis.value < -AXIS_DEAD_ZONE) {
                            m_scrollX += 20;
                        } else if (event.caxis.value > AXIS_DEAD_ZONE) {
                            m_scrollX -= 20;
                        }
                        break;
                    case SDL_CONTROLLER_AXIS_LEFTY:
                    case SDL_CONTROLLER_AXIS_RIGHTY:
                        if (event.caxis.value < -AXIS_DEAD_ZONE) {
                            m_scrollY += 20;
                        } else if (event.caxis.value > AXIS_DEAD_ZONE) {
                            m_scrollY -= 20;
                        }
                        break;
                }
                clampScroll();
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (event.cbutton.which == m_gameControllerInstanceID) {
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        goToNextPage();
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        goToPreviousPage();
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        zoom(10);
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        zoom(-10);
                        break;
                    case SDL_CONTROLLER_BUTTON_A:
                        resetPageView();
                        break;
                    case SDL_CONTROLLER_BUTTON_B:
                        printAppState();
                        break;
                    case SDL_CONTROLLER_BUTTON_X:
                        goToPage(0);
                        break;
                    case SDL_CONTROLLER_BUTTON_Y:
                        goToPage(m_pageCount - 1);
                        break;
                }
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (m_gameController == nullptr) {
                m_gameController = SDL_GameControllerOpen(event.cdevice.which);
                if (m_gameController) {
                    m_gameControllerInstanceID = SDL_JoystickGetDeviceInstanceID(event.cdevice.which);
                    std::cout << "Opened game controller: " << SDL_GameControllerName(m_gameController) << std::endl;
                } else {
                    std::cerr << "Could not open game controller: " << SDL_GetError() << std::endl;
                }
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (m_gameController != nullptr && event.cdevice.which == m_gameControllerInstanceID) {
                SDL_GameControllerClose(m_gameController);
                m_gameController = nullptr;
                m_gameControllerInstanceID = -1;
                std::cout << "Game controller disconnected." << std::endl;
            }
            break;
    }

    if (action == AppAction::Quit) {
        m_running = false;
    } else if (action == AppAction::Resize) {
        fitPageToWindow();
    }
}

void App::loadDocument() {
    m_currentPage = 0;
    fitPageToWindow();
}

void App::renderCurrentPage() {
    m_renderer->clear(255, 255, 255, 255);

    int currentWindowWidth = m_renderer->getWindowWidth();
    int currentWindowHeight = m_renderer->getWindowHeight();

    int renderedWidth, renderedHeight;
    std::vector<uint8_t> pixelData = m_document->renderPage(m_currentPage, renderedWidth, renderedHeight, m_currentScale);

    m_pageWidth = renderedWidth;
    m_pageHeight = renderedHeight;

    int posX = (currentWindowWidth - m_pageWidth) / 2 + m_scrollX;
    int posY = (currentWindowHeight - m_pageHeight) / 2 + m_scrollY;

    m_renderer->renderPage(pixelData, renderedWidth, renderedHeight, posX, posY, m_pageWidth, m_pageHeight);
}

void App::renderUI() {
    int baseFontSize = 16;
    m_textRenderer->setFontSize(baseFontSize);

    SDL_Color textColor = { 0, 0, 0, 255 };
    std::string pageInfo = "Page: " + std::to_string(m_currentPage + 1) + "/" + std::to_string(m_pageCount);
    std::string scaleInfo = "Scale: " + std::to_string(m_currentScale) + "%";

    int currentWindowWidth = m_renderer->getWindowWidth();
    int currentWindowHeight = m_renderer->getWindowHeight();

    m_textRenderer->renderText(pageInfo,
                               (currentWindowWidth - static_cast<int>(pageInfo.length()) * 8) / 2,
                               currentWindowHeight - 30, textColor);

    m_textRenderer->renderText(scaleInfo,
                               currentWindowWidth - static_cast<int>(scaleInfo.length()) * 8 - 10,
                               10, textColor);
}

void App::goToNextPage() {
    if (m_currentPage < m_pageCount - 1) {
        m_currentPage++;
        fitPageToWindow();
    }
}

void App::goToPreviousPage() {
    if (m_currentPage > 0) {
        m_currentPage--;
        fitPageToWindow();
    }
}

void App::goToPage(int pageNum) {
    if (pageNum >= 0 && pageNum < m_pageCount) {
        m_currentPage = pageNum;
        fitPageToWindow();
    }
}

void App::zoom(int delta) {
    int oldScale = m_currentScale;
    m_currentScale += delta;
    if (m_currentScale < 10) m_currentScale = 10;
    if (m_currentScale > 500) m_currentScale = 500;

    recenterScrollOnZoom(oldScale, m_currentScale);
    clampScroll();
}

void App::zoomTo(int scale) {
    int oldScale = m_currentScale;
    m_currentScale = scale;
    if (m_currentScale < 10) m_currentScale = 10;
    if (m_currentScale > 500) m_currentScale = 500;

    recenterScrollOnZoom(oldScale, m_currentScale);
    clampScroll();
}


void App::fitPageToWindow() {
    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

    int nativeWidth = m_document->getPageWidthNative(m_currentPage);
    int nativeHeight = m_document->getPageHeightNative(m_currentPage);

    if (nativeWidth == 0 || nativeHeight == 0) {
        std::cerr << "App ERROR: Native page dimensions are zero for page " << m_currentPage << std::endl;
        return;
    }

    int scaleToFitWidth = static_cast<int>((static_cast<double>(windowWidth) / nativeWidth) * 100.0);
    int scaleToFitHeight = static_cast<int>((static_cast<double>(windowHeight) / nativeHeight) * 100.0);

    m_currentScale = std::min(scaleToFitWidth, scaleToFitHeight);

    if (m_currentScale < 10) m_currentScale = 10;
    if (m_currentScale > 500) m_currentScale = 500;

    m_pageWidth = static_cast<int>(nativeWidth * (m_currentScale / 100.0));
    m_pageHeight = static_cast<int>(nativeHeight * (m_currentScale / 100.0));

    m_scrollX = 0;
    m_scrollY = 0;

}

void App::recenterScrollOnZoom(int oldScale, int newScale) {
    if (oldScale == 0 || newScale == 0) return;

    int nativeWidth = m_document->getPageWidthNative(m_currentPage);
    int nativeHeight = m_document->getPageHeightNative(m_currentPage);

    int oldPageWidth = static_cast<int>(nativeWidth * (oldScale / 100.0));
    int oldPageHeight = static_cast<int>(nativeHeight * (oldScale / 100.0));

    int newPageWidth = static_cast<int>(nativeWidth * (newScale / 100.0));
    int newPageHeight = static_cast<int>(nativeHeight * (newScale / 100.0));

    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

    int viewportCenterX = (windowWidth / 2) - m_scrollX;
    int viewportCenterY = (windowHeight / 2) - m_scrollY;

    int oldRelativeX = viewportCenterX - (windowWidth - oldPageWidth) / 2;
    int oldRelativeY = viewportCenterY - (windowHeight - oldPageHeight) / 2;

    int newRelativeX = static_cast<int>(oldRelativeX * (static_cast<double>(newPageWidth) / oldPageWidth));
    int newRelativeY = static_cast<int>(oldRelativeY * (static_cast<double>(newPageHeight) / oldPageHeight));

    m_scrollX = (windowWidth / 2) - newRelativeX - (windowWidth - newPageWidth) / 2;
    m_scrollY = (windowHeight / 2) - newRelativeY - (windowHeight - newPageHeight) / 2;

}


void App::clampScroll() {
    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

    int maxScrollX = std::max(0, (m_pageWidth - windowWidth) / 2);
    int maxScrollY = std::max(0, (m_pageHeight - windowHeight) / 2);

    m_scrollX = std::max(-maxScrollX, std::min(maxScrollX, m_scrollX));
    m_scrollY = std::max(-maxScrollY, std::min(maxScrollY, m_scrollY));
}

void App::resetPageView() {
    m_currentPage = 0;
    m_currentScale = 100;
    fitPageToWindow();
}

void App::printAppState() {
    std::cout << "--- App State ---" << std::endl;
    std::cout << "Current Page: " << (m_currentPage + 1) << "/" << m_pageCount << std::endl;
    std::cout << "Native Page Dimensions: "
              << m_document->getPageWidthNative(m_currentPage) << "x"
              << m_document->getPageHeightNative(m_currentPage) << std::endl;
    std::cout << "Current Scale: " << m_currentScale << "%" << std::endl;
    std::cout << "Scaled Page Dimensions: " << m_pageWidth << "x" << m_pageHeight << " (Expected/Actual)" << std::endl;
    std::cout << "Scroll Position (Page Offset): X=" << m_scrollX << ", Y=" << m_scrollY << std::endl;
    std::cout << "Window Dimensions: " << m_renderer->getWindowWidth() << "x" << m_renderer->getWindowHeight() << std::endl;
    std::cout << "-----------------" << std::endl;
}


void App::initializeGameControllers() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            m_gameController = SDL_GameControllerOpen(i);
            if (m_gameController) {
                m_gameControllerInstanceID = SDL_JoystickGetDeviceInstanceID(i);
                std::cout << "Opened game controller: " << SDL_GameControllerName(m_gameController) << std::endl;
                break;
            } else {
                std::cerr << "Could not open game controller: " << SDL_GetError() << std::endl;
            }
        }
    }
}

void App::closeGameControllers() {
    if (m_gameController) {
        SDL_GameControllerClose(m_gameController);
        m_gameController = nullptr;
        m_gameControllerInstanceID = -1;
        std::cout << "Closed game controller." << std::endl;
    }
}