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

    switch (event.type) {
        case SDL_QUIT: 
            m_running = false;
            break;
        case SDL_WINDOWEVENT: 
            if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                needsRender = true; 
            }
            break;
        case SDL_KEYDOWN: 
            handleKeyDown(event.key.keysym.sym, static_cast<SDL_Keymod>(event.key.keysym.mod));
            break;
        case SDL_MOUSEWHEEL: 
            handleMouseWheel(event.wheel.y, SDL_GetModState()); 
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
                int x_rel = event.motion.x - static_cast<int>(m_lastTouchX);
                int y_rel = event.motion.y - static_cast<int>(m_lastTouchY);
                handleMouseMotion(x_rel, y_rel, event.motion.state);
                m_lastTouchX = static_cast<float>(event.motion.x);
                m_lastTouchY = static_cast<float>(event.motion.y);
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
        case SDL_CONTROLLERBUTTONDOWN: {
            bool controllerNeedsRender = false;
            switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    controllerNeedsRender = changeScroll(0, -32);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    controllerNeedsRender = changeScroll(0, 32);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    controllerNeedsRender = changeScroll(-32, 0);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    controllerNeedsRender = changeScroll(32, 0);
                    break;
                case SDL_CONTROLLER_BUTTON_A: // Next Page
                    controllerNeedsRender = changePage(1);
                    break;
                case SDL_CONTROLLER_BUTTON_B: // Previous Page
                    controllerNeedsRender = changePage(-1);
                    break;
                case SDL_CONTROLLER_BUTTON_X: // Zoom Out
                    controllerNeedsRender = changeScale(-10);
                    break;
                case SDL_CONTROLLER_BUTTON_Y: // Zoom In
                    controllerNeedsRender = changeScale(10);
                    break;
                default:
                    break;
            }
            if (controllerNeedsRender) {
                render();
            }
            break;
        }
        case SDL_CONTROLLERAXISMOTION: {
            bool controllerNeedsRender = false;
            // Axis values range from -32768 to 32767
            const int AXIS_DEAD_ZONE = 8000; // Adjust as needed

            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) { // Left Stick Y-axis for vertical scroll
                if (event.caxis.value < -AXIS_DEAD_ZONE) {
                    controllerNeedsRender = changeScroll(0, -32); // Scroll up
                } else if (event.caxis.value > AXIS_DEAD_ZONE) {
                    controllerNeedsRender = changeScroll(0, 32); // Scroll down
                }
            } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) { // Left Stick X-axis for horizontal scroll
                if (event.caxis.value < -AXIS_DEAD_ZONE) {
                    controllerNeedsRender = changeScroll(-32, 0); // Scroll left
                } else if (event.caxis.value > AXIS_DEAD_ZONE) {
                    controllerNeedsRender = changeScroll(32, 0); // Scroll right
                }
            } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) { // Right Stick Y-axis for zoom
                 if (event.caxis.value < -AXIS_DEAD_ZONE) {
                    controllerNeedsRender = changeScale(5); // Zoom in
                } else if (event.caxis.value > AXIS_DEAD_ZONE) {
                    controllerNeedsRender = changeScale(-5); // Zoom out
                }
            }
            if (controllerNeedsRender) {
                render();
            }
            break;
        }
        default:
            break;
    }
    if (needsRender) {
        render(); 
    }
}

// Handles keyboard key down events.
void App::handleKeyDown(SDL_Keycode key, SDL_Keymod mod) {
    (void)mod; // Suppress unused parameter warning for 'mod'
    bool needsRender = false; 
    switch (key) {
        case SDLK_q: // 'Q' key to quit
        case SDLK_ESCAPE: // Escape key to quit
            m_running = false;
            break;
        case SDLK_RIGHT: // Right arrow to scroll right
            needsRender = changeScroll(32, 0);
            break;
        case SDLK_LEFT: // Left arrow to scroll left
            needsRender = changeScroll(-32, 0);
            break;
        case SDLK_UP: // Up arrow to scroll up
            needsRender = changeScroll(0, -32);
            break;
        case SDLK_DOWN: // Down arrow to scroll down
            needsRender = changeScroll(0, 32);
            break;
        case SDLK_PAGEDOWN: // Page Down key for next page
            needsRender = changePage(1);
            break;
        case SDLK_PAGEUP: // Page Up key for previous page
            needsRender = changePage(-1);
            break;
        case SDLK_EQUALS: // '=' key (often for '+') to zoom in
            needsRender = changeScale(10);
            break;
        case SDLK_MINUS: // '-' key to zoom out
            needsRender = changeScale(-10);
            break;
        case SDLK_f: // 'F' key to toggle fullscreen
            m_renderer->toggleFullscreen();
            needsRender = true; // Always re-render after fullscreen change
            break;
        default:
            break;
    }
    if (needsRender) {
        render(); 
    }
}

// mouse wheel for scroll, CTRL mouse wheel for zoom
void App::handleMouseWheel(int y_delta, SDL_Keymod mod) {
    bool needsRender = false;
    if (mod & (KMOD_LCTRL | KMOD_RCTRL)) {  
        needsRender = changeScale(y_delta * 5); // Zoom sensitivity
    } else {
        needsRender = changeScroll(0, -y_delta * 32); // Vertical scroll sensitivity
    }
    if (needsRender) {
        render();
    }
}

void App::handleMouseMotion(int x_rel, int y_rel, Uint32 mouse_state) {
    (void)mouse_state; // Suppress unused parameter warning for 'mouse_state'
    // The m_isDragging flag now controls if this function is called.
    // The mouse_state is passed for completeness but its primary check (left button)
    // is now done in SDL_MOUSEBUTTONDOWN/UP.
    bool needsRender = changeScroll(-x_rel, -y_rel); // Invert for natural drag direction
    if (needsRender) {
        render();
    }
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
        m_textRenderer->setFontSize(m_currentScale);
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
    m_textRenderer->setFontSize(m_currentScale);

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
