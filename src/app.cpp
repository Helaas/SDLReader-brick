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
      m_isDragging(false), m_lastTouchX(0.0f), m_lastTouchY(0.0f),
      m_gameController(nullptr), m_gameControllerInstanceID(-1) { 


    m_renderer = std::make_unique<Renderer>(initialWidth, initialHeight, "SDLBook C++");


    m_textRenderer = std::make_unique<TextRenderer>(m_renderer->getSDLRenderer(), "res/Roboto-Regular.ttf", 16);


    if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".pdf") {
        m_document = std::make_unique<PdfDocument>();
    } else if (filename.length() >= 5 && filename.substr(filename.length() - 5) == ".djvu") {
        m_document = std::make_unique<DjvuDocument>();
    } else {
        throw std::runtime_error("Unsupported file format. Please provide a .pdf or .djvu file.");
    }

    if (!m_document->open(filename)) {
        throw std::runtime_error("Failed to open document: " + filename);
    }

    m_pageCount = m_document->getPageCount();
    if (m_pageCount == 0) {
        throw std::runtime_error("Document contains no pages.");
    }

    renderCurrentPage();
}


void App::run() {
    SDL_Event event;
    while (m_running) {
        while (SDL_PollEvent(&event)) {
            handleEvent(event); 
        }

        update();

        render();

        SDL_Delay(1);
    }
}

// --- Event Handling ---

void App::handleEvent(const SDL_Event& event) {
    bool needsRender = false;
    AppAction action = AppAction::None;
    int deltaValue = 0; // Used for scroll/zoom deltas

    switch (event.type) {
        case SDL_QUIT:
            action = AppAction::Quit;
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                action = AppAction::Resize;
            }
            break;
        case SDL_KEYDOWN:
        // clang-format off
            switch (event.key.keysym.sym) {
                case SDLK_q:            action = AppAction::Quit; break;
                case SDLK_ESCAPE:       action = AppAction::Quit; break;
                case SDLK_RIGHT:        action = AppAction::ScrollRight; break;
                case SDLK_LEFT:         action = AppAction::ScrollLeft; break;
                case SDLK_UP:           action = AppAction::ScrollUp; break;
                case SDLK_DOWN:         action = AppAction::ScrollDown; break;
                case SDLK_PAGEDOWN:     action = AppAction::PageNext; break;
                case SDLK_PAGEUP:       action = AppAction::PagePrevious; break;
                case SDLK_EQUALS:       action = AppAction::ZoomIn; break;
                case SDLK_MINUS:        action = AppAction::ZoomOut; break;
                case SDLK_f:            action = AppAction::ToggleFullscreen; break;
                default: break;
            }
        // clang-format on
            break;
        case SDL_MOUSEWHEEL:
            action = mapMouseWheelToAppAction(event.wheel.y, SDL_GetModState(), deltaValue);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_isDragging = true;
                m_lastTouchX = static_cast<float>(event.button.x);
                m_lastTouchY = static_cast<float>(event.button.y);
                action = AppAction::DragStart; // Or simply set m_isDragging
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_isDragging = false;
                action = AppAction::DragEnd; // Or simply clear m_isDragging
            }
            break;
        case SDL_MOUSEMOTION:
            if (m_isDragging) {
                int x_rel = event.motion.x - static_cast<int>(m_lastTouchX);
                int y_rel = event.motion.y - static_cast<int>(m_lastTouchY);
                needsRender = changeScroll(-x_rel, -y_rel); // Invert for natural drag direction
                m_lastTouchX = static_cast<float>(event.motion.x);
                m_lastTouchY = static_cast<float>(event.button.y);
            }
            break;

        // --- Touch Events (SDL_FINGER*) ---
        case SDL_FINGERDOWN:
            m_isDragging = true;
            // Store normalized touch coordinates (0.0 - 1.0)
            m_lastTouchX = event.tfinger.x;
            m_lastTouchY = event.tfinger.y;
            break;
        case SDL_FINGERUP:
            m_isDragging = false;
            break;
        case SDL_FINGERMOTION: {
            if (m_isDragging) {
                // Convert normalized relative motion to pixel motion
                int windowWidth = m_renderer->getWindowWidth();
                int windowHeight = m_renderer->getWindowHeight();

                // Calculate pixel-based relative motion
                int x_rel_pixels = static_cast<int>((event.tfinger.x - m_lastTouchX) * windowWidth);
                int y_rel_pixels = static_cast<int>((event.tfinger.y - m_lastTouchY) * windowHeight);

                // Use the existing changeScroll for panning
                needsRender = changeScroll(-x_rel_pixels, -y_rel_pixels); // Invert for natural drag direction

                // Update last normalized touch position
                m_lastTouchX = event.tfinger.x;
                m_lastTouchY = event.tfinger.y;
            }
            break;
        }

        // --- Controller Events (WiiU style, mapped to generic SDL_CONTROLLER*) ---
        case SDL_CONTROLLERDEVICEADDED: {
            // Check if a controller is already open, if not, open the new one
            if (!m_gameController) {
                m_gameController = SDL_GameControllerOpen(event.cdevice.which);
                if (m_gameController) {
                    m_gameControllerInstanceID = event.cdevice.which;
                    std::cout << "Controller Added: " << SDL_GameControllerName(m_gameController) << std::endl;
                } else {
                    std::cerr << "Could not open game controller: " << SDL_GetError() << std::endl;
                }
            }
            break;
        }
        case SDL_CONTROLLERDEVICEREMOVED: {
            // Check if the removed controller's instance ID matches the one we are managing
            if (m_gameController && event.cdevice.which == m_gameControllerInstanceID) {
                std::cout << "Controller Removed: " << SDL_GameControllerName(m_gameController) << std::endl;
                SDL_GameControllerClose(m_gameController);
                m_gameController = nullptr;
                m_gameControllerInstanceID = -1;
            }
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
        // clang-format off
            switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:     action = AppAction::ScrollUp; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:   action = AppAction::ScrollDown; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:   action = AppAction::ScrollLeft; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:  action = AppAction::ScrollRight; break;
                case SDL_CONTROLLER_BUTTON_A:           action = AppAction::PageNext; break;
                case SDL_CONTROLLER_BUTTON_B:           action = AppAction::PagePrevious; break;
                case SDL_CONTROLLER_BUTTON_X:           action = AppAction::ZoomOut; deltaValue = -10; break;
                case SDL_CONTROLLER_BUTTON_Y:           action = AppAction::ZoomIn; deltaValue = 10; break;
                default: break;
            }
        // clang-format on
            break;
        case SDL_CONTROLLERAXISMOTION: {
            // Axis values range from -32768 to 32767
            const int AXIS_DEAD_ZONE = 8000; // Adjust as needed
            if (std::abs(event.caxis.value) > AXIS_DEAD_ZONE) {
                action = mapControllerAxisToAppAction(static_cast<SDL_GameControllerAxis>(event.caxis.axis), event.caxis.value, deltaValue);
            }
            break;
        }
        default:
            break;
    }

    if (action != AppAction::None) {
        needsRender = processAppAction(action, deltaValue);
    }

    if (needsRender) {
        render();
    }
}

// Maps mouse wheel event to AppAction and returns associated delta
App::AppAction App::mapMouseWheelToAppAction(int y_delta, SDL_Keymod mod, int& deltaValue) {
    if (mod & (KMOD_LCTRL | KMOD_RCTRL)) {
        deltaValue = y_delta * 5; // Zoom sensitivity
        return (y_delta > 0) ? AppAction::ZoomIn : AppAction::ZoomOut;
    } else {
        deltaValue = -y_delta * 32; // Vertical scroll sensitivity
        return (y_delta > 0) ? AppAction::ScrollUp : AppAction::ScrollDown;
    }
}

// Maps controller axis motion to AppAction and returns associated delta
App::AppAction App::mapControllerAxisToAppAction(SDL_GameControllerAxis axis, Sint16 value, int& deltaValue) {
    // Determine scroll/zoom delta based on axis value (e.g., fixed step or scaled)
    if (axis == SDL_CONTROLLER_AXIS_LEFTY) { // Left Stick Y-axis for vertical scroll
        deltaValue = (value < 0) ? -32 : 32;
        return (value < 0) ? AppAction::ScrollUp : AppAction::ScrollDown;
    } else if (axis == SDL_CONTROLLER_AXIS_LEFTX) { // Left Stick X-axis for horizontal scroll
        deltaValue = (value < 0) ? -32 : 32;
        return (value < 0) ? AppAction::ScrollLeft : AppAction::ScrollRight;
    } else if (axis == SDL_CONTROLLER_AXIS_RIGHTY) { // Right Stick Y-axis for zoom
        deltaValue = (value < 0) ? 5 : -5;
        return (value < 0) ? AppAction::ZoomIn : AppAction::ZoomOut;
    }
    return AppAction::None;
}

// Processes an AppAction
bool App::processAppAction(AppAction action, int deltaValue) {
    bool needsRender = false;
    // clang-format off
    switch (action) {
        case AppAction::Quit:           m_running = false; break;
        case AppAction::Resize:         needsRender = true; break; // Window resized/exposed, force re-render
        case AppAction::ScrollLeft:     needsRender = changeScroll(-32, 0); break;
        case AppAction::ScrollRight:    needsRender = changeScroll(32, 0); break;
        case AppAction::ScrollUp:       needsRender = changeScroll(0, -32); break;
        case AppAction::ScrollDown:     needsRender = changeScroll(0, 32); break;
        case AppAction::PageNext:       needsRender = changePage(1); break;
        case AppAction::PagePrevious:   needsRender = changePage(-1); break;
        case AppAction::ZoomIn: {
            needsRender = changeScale(deltaValue == 0 ? 10 : deltaValue); // Use default if deltaValue not set
            break;
        }
        case AppAction::ZoomOut: {
            needsRender = changeScale(deltaValue == 0 ? -10 : deltaValue); // Use default if deltaValue not set
            break;
        }
        case AppAction::ToggleFullscreen: {
            m_renderer->toggleFullscreen();
            needsRender = true;
            break;
        }
        default: break;
    }
    // clang-format on
    return needsRender;
}

// --- State Update Methods ---

bool App::changePage(int delta) {
    int newPage = m_currentPage + delta;
    if (newPage >= 0 && newPage < m_pageCount) {
        m_currentPage = newPage;
        m_scrollX = 0; 
        m_scrollY = 0;
        renderCurrentPage(); 
        return true;
    }
    return false; 
}

// Jumps to a specific page number.
bool App::jumpToPage(int pageNum) {
    if (pageNum >= 0 && pageNum < m_pageCount) {
        m_currentPage = pageNum;
        m_scrollX = 0; 
        m_scrollY = 0;
        renderCurrentPage();
        return true;
    }
    return false;
}

// Changes the zoom scale.
bool App::changeScale(int delta) {
    int newScale = m_currentScale + delta;
    // Clamp the new scale within a reasonable range (e.g., 10% to 500%).
    if (newScale >= 10 && newScale <= 500) {
        m_currentScale = newScale;
        m_textRenderer->setFontSize(m_currentScale / 10); // Adjust font size more reasonably
        renderCurrentPage(); 
        m_scrollX = std::min(m_scrollX, std::max(0, m_pageWidth - m_renderer->getWindowWidth()));
        m_scrollY = std::min(m_scrollY, std::max(0, m_pageHeight - m_renderer->getWindowHeight()));
        return true;
    }
    return false;
}

bool App::changeScroll(int deltaX, int deltaY) {
    int currentWindowWidth = m_renderer->getWindowWidth();
    int currentWindowHeight = m_renderer->getWindowHeight();

    int newScrollX = m_scrollX + deltaX;
    int newScrollY = m_scrollY + deltaY;

    // Calculate maximum scroll values. If the page is smaller than the window, max scroll is 0.
    int maxScrollX = std::max(0, m_pageWidth - currentWindowWidth);
    int maxScrollY = std::max(0, m_pageHeight - currentWindowHeight);

    // Clamp new scroll values to stay within the valid range [0, maxScroll].
    newScrollX = std::max(0, std::min(newScrollX, maxScrollX));
    newScrollY = std::max(0, std::min(newScrollY, maxScrollY));

    if (newScrollX != m_scrollX || newScrollY != m_scrollY) {
        m_scrollX = newScrollX;
        m_scrollY = newScrollY;
        return true;
    }
    return false;
}

// Placeholder for continuous update logic. Progress bars, cute animations?
void App::update() {
    // For a static book reader, there are typically no continuous updates needed.
    // All changes are driven by user input events.
}

// --- Rendering Methods ---

void App::renderCurrentPage() {

    std::vector<uint8_t> pagePixels = m_document->renderPage(m_currentPage, m_pageWidth, m_pageHeight, m_currentScale);

    m_renderer->clear(255, 255, 255, 255);

    int currentWindowWidth = m_renderer->getWindowWidth();
    int currentWindowHeight = m_renderer->getWindowHeight();

    int destX, destY; 

    // Calculate horizontal positioning:
    if (m_pageWidth <= currentWindowWidth) {
        // If the page is narrower than or equal to the window width, center it horizontally.
        destX = (currentWindowWidth - m_pageWidth) / 2;
    } else {
        // If the page is wider, position it based on the horizontal scroll offset.
        // A positive m_scrollX means the content is shifted left relative to the window.
        destX = -m_scrollX;
    }

    // Calculate vertical positioning:
    if (m_pageHeight <= currentWindowHeight) {
        // If the page is shorter than or equal to the window height, center it vertically.
        destY = (currentWindowHeight - m_pageHeight) / 2;
    } else {
        // If the page is taller, position it based on the vertical scroll offset.
        // A positive m_scrollY means the content is shifted up relative to the window.
        destY = -m_scrollY;
    }

    m_renderer->renderPage(pagePixels, m_pageWidth, m_pageHeight,
                           destX, destY, m_pageWidth, m_pageHeight);

    renderUIOverlay();

    m_renderer->present();
}

// Renders the UI overlay (page number and scale information).
void App::renderUIOverlay() {
    // Ensure the font size is updated based on the current scale before rendering text.
    // Consider scaling the font size more reasonably, e.g., for 100% zoom, a default font size.
    // The current approach `m_currentScale` directly will make the font huge at high zooms.
    // Let's set a base font size and apply a smaller scaling factor or a fixed size.
    int baseFontSize = 16;
    m_textRenderer->setFontSize(baseFontSize); // Keep font size consistent regardless of document zoom

    SDL_Color textColor = { 0, 0, 0, 255 };
    std::string pageInfo = "Page: " + std::to_string(m_currentPage + 1) + "/" + std::to_string(m_pageCount);
    std::string scaleInfo = "Scale: " + std::to_string(m_currentScale) + "%";

    int currentWindowWidth = m_renderer->getWindowWidth();
    int currentWindowHeight = m_renderer->getWindowHeight();

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
    renderCurrentPage();
}
