#include "app.h"
#include "renderer.h"
#include "text_renderer.h"
#include "document.h"
#include "mupdf_document.h"
#include "page_preloader.h"
#ifdef TG5040_PLATFORM
#include "power_handler.h"
#endif

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cmath>

// --- App Class ---

// Constructor now accepts pre-initialized SDL_Window* and SDL_Renderer*
App::App(const std::string &filename, SDL_Window *window, SDL_Renderer *renderer)
    : m_running(true), m_currentPage(0), m_currentScale(100),
      m_scrollX(0), m_scrollY(0), m_pageWidth(0), m_pageHeight(0),
      m_isDragging(false), m_lastTouchX(0.0f), m_lastTouchY(0.0f),
      m_gameController(nullptr), m_gameControllerInstanceID(-1),
      m_lastZoomTime(std::chrono::steady_clock::now())
{

    // Pass the pre-initialized window and renderer to the Renderer object
    m_renderer = std::make_unique<Renderer>(window, renderer);

    m_textRenderer = std::make_unique<TextRenderer>(m_renderer->getSDLRenderer(), "res/Roboto-Regular.ttf", 16);

#ifdef TG5040_PLATFORM
    // Initialize power handler
    m_powerHandler = std::make_unique<PowerHandler>();
    
    // Register error callback for displaying GUI messages
    m_powerHandler->setErrorCallback([this](const std::string& message) {
        showErrorMessage(message);
    });
    
    // Register sleep mode callback for fake sleep functionality
    m_powerHandler->setSleepModeCallback([this](bool enterFakeSleep) {
        m_inFakeSleep = enterFakeSleep;
        if (enterFakeSleep) {
            std::cout << "App: Entering fake sleep mode - disabling inputs, screen will go black" << std::endl;
        } else {
            std::cout << "App: Exiting fake sleep mode - re-enabling inputs and screen" << std::endl;
        }
    });
#endif

    // Determine document type based on file extension
    // MuPDF supports PDF, CBZ, ZIP (with images), XPS, EPUB, and other formats
    std::string lowercaseFilename = filename;
    std::transform(lowercaseFilename.begin(), lowercaseFilename.end(), 
                   lowercaseFilename.begin(), ::tolower);
    
    // MuPDF can handle all these formats through its generic document interface
    if ((lowercaseFilename.size() >= 4 && 
         (lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".pdf" ||
          lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".cbz" ||
          lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".zip")) ||
        (lowercaseFilename.size() >= 5 && 
         lowercaseFilename.substr(lowercaseFilename.size() - 5) == ".epub")) {
        m_document = std::make_unique<MuPdfDocument>();
    }
    else
    {
        throw std::runtime_error("Unsupported file format: " + filename + 
                                " (supported: .pdf, .cbz, .zip)");
    }

    if (!m_document->open(filename))
    {
        throw std::runtime_error("Failed to open document: " + filename);
    }

    // Set max render size for downsampling based on current window size
    if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get()))
    {
        muDoc->setMaxRenderSize(m_renderer->getWindowWidth(), m_renderer->getWindowHeight());
    }

    m_pageCount = m_document->getPageCount();
    if (m_pageCount == 0)
    {
        throw std::runtime_error("Document contains no pages: " + filename);
    }

    // Initial page load and fit
    loadDocument();

    // Initialize scale display timer
    m_scaleDisplayTime = SDL_GetTicks();
    
    // Initialize page display timer  
    m_pageDisplayTime = SDL_GetTicks();

    // Initialize game controllers
    initializeGameControllers();
    
    // Initialize page preloader
    m_pagePreloader = std::make_unique<PagePreloader>(this, m_document.get());
    m_pagePreloader->start();
    
    // Start preloading pages ahead of current page
    m_pagePreloader->requestPreload(m_currentPage, m_currentScale);
}

App::~App()
{
    // Stop page preloader first
    if (m_pagePreloader) {
        m_pagePreloader->stop();
    }
    
#ifdef TG5040_PLATFORM
    if (m_powerHandler) {
        m_powerHandler->stop();
    }
#endif
    closeGameControllers();
}

void App::run()
{
    m_prevTick = SDL_GetTicks();

#ifdef TG5040_PLATFORM
    // Start power button monitoring
    if (!m_powerHandler->start()) {
        std::cerr << "Warning: Failed to start power button monitoring" << std::endl;
    }
#endif

    SDL_Event event;
    while (m_running)
    {
        while (SDL_PollEvent(&event) != 0)
        {
            // In fake sleep mode, ignore all SDL events (power button is handled by PowerHandler)
            if (!m_inFakeSleep) {
                handleEvent(event);
            } else {
                // Only handle quit events to allow graceful shutdown
                if (event.type == SDL_QUIT) {
                    handleEvent(event);
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - m_prevTick) / 1000.0f;
        m_prevTick = now;

        if (!m_inFakeSleep) {
            // Normal rendering - only render if something changed
            bool panningChanged = updateHeldPanning(dt);
            
            if (m_needsRedraw || panningChanged) {
                renderCurrentPage();
                renderUI();
                m_renderer->present();
                m_needsRedraw = false;
            }
        } else {
            // Fake sleep mode - render black screen
            if (m_needsRedraw) {
                SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), 0, 0, 0, 255);
                SDL_RenderClear(m_renderer->getSDLRenderer());
                m_renderer->present();
                m_needsRedraw = false;
            }
        }
    }
}

void App::handleEvent(const SDL_Event &event)
{
    AppAction action = AppAction::None;

    switch (event.type)
    {
    case SDL_QUIT:
        action = AppAction::Quit;
        break;
    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
            event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
            action = AppAction::Resize;
        }
        break;
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym)
        {
        case SDLK_AC_HOME:
            action = AppAction::Quit;
            break;
        case SDLK_ESCAPE:
        case SDLK_q:
            if (m_pageJumpInputActive) {
                cancelPageJumpInput();
            } else {
                action = AppAction::Quit;
            }
            break;
        case SDLK_RIGHT:
            if (!isInScrollTimeout()) {
                m_scrollX -= 50;
                clampScroll();
                updatePageDisplayTime();
                markDirty();
            }
            break;
        case SDLK_LEFT:
            if (!isInScrollTimeout()) {
                m_scrollX += 50;
                clampScroll();
                updatePageDisplayTime();
                markDirty();
            }
            break;
        case SDLK_UP:
            if (!isInScrollTimeout()) {
                m_scrollY += 50;
                clampScroll();
                updatePageDisplayTime();
                markDirty();
            }
            break;
        case SDLK_DOWN:
            if (!isInScrollTimeout()) {
                m_scrollY -= 50;
                clampScroll();
                updatePageDisplayTime();
                markDirty();
            }
            break;
        case SDLK_PAGEDOWN:
            if (!isInPageChangeCooldown()) {
                goToNextPage();
            }
            break;
        case SDLK_PAGEUP:
            if (!isInPageChangeCooldown()) {
                goToPreviousPage();
            }
            break;
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
            zoom(10);
            break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            zoom(-10);
            break;
        case SDLK_HOME:
            goToPage(0);
            break;
        case SDLK_END:
            goToPage(m_pageCount - 1);
            break;
        case SDLK_0:
        case SDLK_KP_0:
            if (m_pageJumpInputActive) {
                handlePageJumpInput('0');
            } else {
                zoomTo(100);
            }
            break;
        case SDLK_1:
        case SDLK_KP_1:
            if (m_pageJumpInputActive) {
                handlePageJumpInput('1');
            }
            break;
        case SDLK_2:
        case SDLK_KP_2:
            if (m_pageJumpInputActive) {
                handlePageJumpInput('2');
            }
            break;
        case SDLK_3:
        case SDLK_KP_3:
            if (m_pageJumpInputActive) {
                handlePageJumpInput('3');
            }
            break;
        case SDLK_4:
        case SDLK_KP_4:
            if (m_pageJumpInputActive) {
                handlePageJumpInput('4');
            }
            break;
        case SDLK_5:
        case SDLK_KP_5:
            if (m_pageJumpInputActive) {
                handlePageJumpInput('5');
            }
            break;
        case SDLK_6:
        case SDLK_KP_6:
            if (m_pageJumpInputActive) {
                handlePageJumpInput('6');
            }
            break;
        case SDLK_7:
        case SDLK_KP_7:
            if (m_pageJumpInputActive) {
                handlePageJumpInput('7');
            }
            break;
        case SDLK_8:
        case SDLK_KP_8:
            if (m_pageJumpInputActive) {
                handlePageJumpInput('8');
            }
            break;
        case SDLK_9:
        case SDLK_KP_9:
            if (m_pageJumpInputActive) {
                handlePageJumpInput('9');
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (m_pageJumpInputActive) {
                confirmPageJumpInput();
            }
            break;
        case SDLK_f:
            m_renderer->toggleFullscreen();
            fitPageToWindow();
            break;
        case SDLK_g:
            startPageJumpInput();
            break;
        case SDLK_p:
            printAppState();
            break;
        case SDLK_c:
            clampScroll();
            break;
        case SDLK_w:
            fitPageToWidth();
            break;
        case SDLK_r:
            if (SDL_GetModState() & KMOD_SHIFT) {
                rotateClockwise();
            } else {
                resetPageView();
            }
            break;
        case SDLK_h:
            toggleMirrorHorizontal();
            break;
        case SDLK_v:
            toggleMirrorVertical();
            break;
        case SDLK_LEFTBRACKET:
            if (!isInPageChangeCooldown()) {
                jumpPages(-10);
            }
            break;
        case SDLK_RIGHTBRACKET:
            if (!isInPageChangeCooldown()) {
                jumpPages(+10);
            }
            break;
        }
        break;
    case SDL_MOUSEWHEEL:
        if (event.wheel.y > 0)
        {
            if (SDL_GetModState() & KMOD_CTRL)
            {
                zoom(5);
            }
            else if (!isInScrollTimeout())
            {
                m_scrollY += 50;
                updatePageDisplayTime();
            }
        }
        else if (event.wheel.y < 0)
        {
            if (SDL_GetModState() & KMOD_CTRL)
            {
                zoom(-5);
            }
            else if (!isInScrollTimeout())
            {
                m_scrollY -= 50;
                updatePageDisplayTime();
            }
        }
        clampScroll();
        markDirty();
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            m_isDragging = true;
            m_lastTouchX = static_cast<float>(event.button.x);
            m_lastTouchY = static_cast<float>(event.button.y);
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            m_isDragging = false;
        }
        break;
    case SDL_MOUSEMOTION:
        if (m_isDragging && !isInScrollTimeout())
        {
            float dx = static_cast<float>(event.motion.x) - m_lastTouchX;
            float dy = static_cast<float>(event.motion.y) - m_lastTouchY;
            m_scrollX += static_cast<int>(dx);
            m_scrollY += static_cast<int>(dy);
            m_lastTouchX = static_cast<float>(event.motion.x);
            m_lastTouchY = static_cast<float>(event.motion.y);
            clampScroll();
            updatePageDisplayTime();
            markDirty();
        }
        break;
    case SDL_CONTROLLERAXISMOTION:
        if (event.caxis.which == m_gameControllerInstanceID)
        {
            const Sint16 AXIS_DEAD_ZONE = 8000;
            // --- L2 / R2 as analog axes: jump ±10 pages on a strong press ---
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT &&
                event.caxis.value > AXIS_DEAD_ZONE)
            {
                if (!isInPageChangeCooldown()) {
                    jumpPages(-10);
                }
            }
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT &&
                event.caxis.value > AXIS_DEAD_ZONE)
            {
                if (!isInPageChangeCooldown()) {
                    jumpPages(+10);
                }
            }

            switch (event.caxis.axis)
            {
            case SDL_CONTROLLER_AXIS_LEFTX:
            case SDL_CONTROLLER_AXIS_RIGHTX:
                if (!isInScrollTimeout()) {
                    if (event.caxis.value < -AXIS_DEAD_ZONE)
                    {
                        m_scrollX += 20;
                    }
                    else if (event.caxis.value > AXIS_DEAD_ZONE)
                    {
                        m_scrollX -= 20;
                    }
                }
                break;
            case SDL_CONTROLLER_AXIS_LEFTY:
            case SDL_CONTROLLER_AXIS_RIGHTY:
                if (!isInScrollTimeout()) {
                    if (event.caxis.value < -AXIS_DEAD_ZONE)
                    {
                        m_scrollY += 20;
                    }
                    else if (event.caxis.value > AXIS_DEAD_ZONE)
                    {
                        m_scrollY -= 20;
                    }
                }
                break;
            }
            clampScroll();
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        if (event.cbutton.which == m_gameControllerInstanceID)
        {
            switch (event.cbutton.button)
            {
            // --- D-Pad pans (Move) ---
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                m_dpadRightHeld = true;
                handleDpadNudgeRight();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                m_dpadLeftHeld = true;
                handleDpadNudgeLeft();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                m_dpadUpHeld = true;
                handleDpadNudgeUp();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                m_dpadDownHeld = true;
                handleDpadNudgeDown();
                break;

            // --- L1 / R1: page up (previous) ---
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                if (!isInPageChangeCooldown()) {
                    goToPreviousPage();
                }
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                if (!isInPageChangeCooldown()) {
                    goToNextPage();
                }
                break;

            // --- Y / B: zoom in/out ---
            case SDL_CONTROLLER_BUTTON_Y:
                zoom(5);
                break;
            case SDL_CONTROLLER_BUTTON_B:
                zoom(-5);
                break;

            // --- X: Rotate ---
            case SDL_CONTROLLER_BUTTON_X:
                rotateClockwise();
                break;

            // --- A: Best fit width ---
            case SDL_CONTROLLER_BUTTON_A:
                fitPageToWidth();
                break;

            // --- MENU/START: Quit ---
            case SDL_CONTROLLER_BUTTON_GUIDE:
                action = AppAction::Quit;
                break; // MENU on Brick

            // --- START for horizontal mirroring  ---    
            case SDL_CONTROLLER_BUTTON_START:
                toggleMirrorHorizontal();
                break;

            // --- BACK for vertical mirroring  ---
            case SDL_CONTROLLER_BUTTON_BACK:
                toggleMirrorVertical();
                break; // SELECT
            default:
                break;
            }
        }
        break;
    case SDL_CONTROLLERBUTTONUP:
        if (event.cbutton.which == m_gameControllerInstanceID)
        {
            switch (event.cbutton.button)
            {
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                m_dpadRightHeld = false;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                m_dpadLeftHeld = false;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                m_dpadUpHeld = false;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                m_dpadDownHeld = false;
                break;
            default:
                break;
            }
        }
        break;
    case SDL_CONTROLLERDEVICEADDED:
        if (m_gameController == nullptr)
        {
            m_gameController = SDL_GameControllerOpen(event.cdevice.which);
            if (m_gameController)
            {
                m_gameControllerInstanceID = SDL_JoystickGetDeviceInstanceID(event.cdevice.which);
                std::cout << "Opened game controller: " << SDL_GameControllerName(m_gameController) << std::endl;
            }
            else
            {
                std::cerr << "Could not open game controller: " << SDL_GetError() << std::endl;
            }
        }
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (m_gameController != nullptr && event.cdevice.which == m_gameControllerInstanceID)
        {
            SDL_GameControllerClose(m_gameController);
            m_gameController = nullptr;
            m_gameControllerInstanceID = -1;
            std::cout << "Game controller disconnected." << std::endl;
        }
        break;
    }

    if (action == AppAction::Quit)
    {
        m_running = false;
    }
    else if (action == AppAction::Resize)
    {
        fitPageToWindow();
        markDirty();
    }
}

void App::loadDocument()
{
    m_currentPage = 0;
    fitPageToWindow();
}

void App::renderCurrentPage()
{
    // Try to use preloaded page first
    if (tryRenderPreloadedPage()) {
        return;
    }
    
    // Fall back to regular rendering if no preloaded page available
    std::cout << "FALLBACK: Using direct render for page " << m_currentPage << " at scale " << m_currentScale << std::endl;
    m_renderer->clear(255, 255, 255, 255);

    int winW = m_renderer->getWindowWidth();
    int winH = m_renderer->getWindowHeight();

    int srcW, srcH;
    std::vector<uint8_t> pixelData;
    {
        // Lock the document mutex to ensure thread-safe access
        std::lock_guard<std::mutex> lock(m_documentMutex);
        pixelData = m_document->renderPage(m_currentPage, srcW, srcH, m_currentScale);
    }

    // displayed page size after rotation
    if (m_rotation % 180 == 0)
    {
        m_pageWidth = srcW;
        m_pageHeight = srcH;
    }
    else
    {
        m_pageWidth = srcH;
        m_pageHeight = srcW;
    }

    int posX = (winW - m_pageWidth) / 2 + m_scrollX;

    int posY;
    if (m_pageHeight <= winH)
    {
        if (m_topAlignWhenFits || m_forceTopAlignNextRender)
            posY = 0;
        else
            posY = (winH - m_pageHeight) / 2;
    }
    else
    {
        posY = (winH - m_pageHeight) / 2 + m_scrollY;
    }
    m_forceTopAlignNextRender = false;

    m_renderer->renderPageEx(pixelData, srcW, srcH,
                             posX, posY, m_pageWidth, m_pageHeight,
                             static_cast<double>(m_rotation),
                             currentFlipFlags());
}

bool App::tryRenderPreloadedPage()
{
    if (!m_pagePreloader) {
        return false;
    }
    
    // Try to get preloaded page
    auto preloadedPage = m_pagePreloader->getPreloadedPage(m_currentPage, m_currentScale);
    if (!preloadedPage) {
        std::cout << "PRELOAD MISS: No preloaded page for page " << m_currentPage << " at scale " << m_currentScale << std::endl;
        return false; // No preloaded page available
    }
    
    std::cout << "PRELOAD HIT: Using preloaded page " << m_currentPage << " at scale " << m_currentScale << std::endl;
    
    // Additional safety check: verify page data is valid before using it
    if (preloadedPage->pixelData.empty() || preloadedPage->width <= 0 || preloadedPage->height <= 0) {
        std::cerr << "App: Invalid preloaded page data for page " << m_currentPage 
                  << ", falling back to direct render" << std::endl;
        return false; // Page data is corrupted, fall back to direct rendering
    }
    
    m_renderer->clear(255, 255, 255, 255);

    int winW = m_renderer->getWindowWidth();
    int winH = m_renderer->getWindowHeight();

    int srcW = preloadedPage->width;
    int srcH = preloadedPage->height;

    // displayed page size after rotation
    if (m_rotation % 180 == 0)
    {
        m_pageWidth = srcW;
        m_pageHeight = srcH;
    }
    else
    {
        m_pageWidth = srcH;
        m_pageHeight = srcW;
    }

    int posX = (winW - m_pageWidth) / 2 + m_scrollX;

    int posY;
    if (m_pageHeight <= winH)
    {
        if (m_topAlignWhenFits || m_forceTopAlignNextRender)
            posY = 0;
        else
            posY = (winH - m_pageHeight) / 2;
    }
    else
    {
        posY = (winH - m_pageHeight) / 2 + m_scrollY;
    }
    m_forceTopAlignNextRender = false;

    // Make a local copy of critical data to avoid race conditions during rendering
    std::vector<uint8_t> localPixelData;
    try {
        localPixelData = preloadedPage->pixelData; // Copy pixel data locally
        
        // Final validation of local copy
        if (localPixelData.empty()) {
            std::cerr << "App: Local pixel data copy is empty for page " << m_currentPage << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "App: Error copying pixel data for page " << m_currentPage 
                  << ": " << e.what() << std::endl;
        return false;
    }

    m_renderer->renderPageEx(localPixelData, srcW, srcH,
                             posX, posY, m_pageWidth, m_pageHeight,
                             static_cast<double>(m_rotation),
                             currentFlipFlags());
    
    return true; // Successfully rendered preloaded page
}

void App::renderUI()
{
    int baseFontSize = 16;
    // setFontSize expects percentage scale, so 100% = normal base size
    m_textRenderer->setFontSize(100);

    SDL_Color textColor = {0, 0, 0, 255};
    std::string pageInfo = "Page: " + std::to_string(m_currentPage + 1) + "/" + std::to_string(m_pageCount);
    std::string scaleInfo = "Scale: " + std::to_string(m_currentScale) + "%";

    int currentWindowWidth = m_renderer->getWindowWidth();
    int currentWindowHeight = m_renderer->getWindowHeight();

    // Only show page info for 2 seconds after it changes
    if ((SDL_GetTicks() - m_pageDisplayTime) < PAGE_DISPLAY_DURATION) {
        m_textRenderer->renderText(pageInfo,
                                   (currentWindowWidth - static_cast<int>(pageInfo.length()) * 8) / 2,
                                   currentWindowHeight - 30, textColor);
    }

    // Only show scale info for 2 seconds after it changes
    if ((SDL_GetTicks() - m_scaleDisplayTime) < SCALE_DISPLAY_DURATION) {
        m_textRenderer->renderText(scaleInfo,
                                   currentWindowWidth - static_cast<int>(scaleInfo.length()) * 8 - 10,
                                   10, textColor);
    }
    
    // Render error message if active
    if (!m_errorMessage.empty() && (SDL_GetTicks() - m_errorMessageTime) < ERROR_MESSAGE_DURATION) {
        SDL_Color errorColor = {255, 255, 255, 255}; // White text
        SDL_Color bgColor = {255, 0, 0, 180}; // Semi-transparent red background
        
        // Use larger font for error messages
        // TextRenderer.setFontSize expects a percentage scale, not absolute size
        // Base font is 16, we want 64, so we need 400% scale
        int errorFontScale = 400; // 400% = 4x larger 
        m_textRenderer->setFontSize(errorFontScale);
        
        // Calculate actual font size for positioning
        int actualFontSize = static_cast<int>(baseFontSize * (errorFontScale / 100.0));
        
        // Split message into two lines if it's too long
        std::string line1, line2;
        // Slightly wider character width estimation to break text into two lines earlier for better visual balance
        int avgCharWidth = actualFontSize * 0.50; 
        int maxCharsPerLine = (currentWindowWidth - 60) / avgCharWidth; // Increased margin from 40 to 60 to further reduce single line capacity
        
        if (static_cast<int>(m_errorMessage.length()) <= maxCharsPerLine) {
            // Single line is fine
            line1 = m_errorMessage;
        } else {
            // Split into two lines, preferably at a space
            size_t splitPos = m_errorMessage.length() / 2;
            
            // Look for a space near the middle to split at
            size_t spacePos = m_errorMessage.find_last_of(' ', splitPos + 10);
            if (spacePos != std::string::npos && spacePos > splitPos - 10) {
                splitPos = spacePos;
            }
            
            line1 = m_errorMessage.substr(0, splitPos);
            line2 = m_errorMessage.substr(splitPos);
            
            // Trim leading space from second line
            if (!line2.empty() && line2[0] == ' ') {
                line2 = line2.substr(1);
            }
        }
        
        // Calculate dimensions for potentially two lines using more accurate character width
        int maxLineWidth = std::max(static_cast<int>(line1.length()), static_cast<int>(line2.length())) * avgCharWidth;
        int totalHeight = line2.empty() ? actualFontSize : (actualFontSize * 2 + 10); // Extra spacing between lines
        
        // Center the message block properly
        int messageX = (currentWindowWidth - maxLineWidth) / 2;
        int messageY = (currentWindowHeight - totalHeight) / 2;
        
        // Draw background rectangle with 10% more extension on each side
        int bgExtension = currentWindowWidth * 0.1; // 10% of screen width extension on each side
        SDL_Rect bgRect = {messageX - 20 - bgExtension/2, messageY - 10, maxLineWidth + 60 + bgExtension, totalHeight + 20};
        SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        SDL_SetRenderDrawBlendMode(m_renderer->getSDLRenderer(), SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(m_renderer->getSDLRenderer(), &bgRect);
        
        // Draw first line - center it and shift 5% to the right
        int line1Width = static_cast<int>(line1.length()) * avgCharWidth;
        int line1X = (currentWindowWidth - line1Width) / 2 + (currentWindowWidth * 0.05); // 5% shift to the right
        m_textRenderer->renderText(line1, line1X, messageY, errorColor);
        
        // Draw second line if it exists
        if (!line2.empty()) {
            int line2Width = static_cast<int>(line2.length()) * avgCharWidth;
            int line2X = (currentWindowWidth - line2Width) / 2 + (currentWindowWidth * 0.05); // 5% shift to the right
            int line2Y = messageY + actualFontSize + 10; // Space between lines
            m_textRenderer->renderText(line2, line2X, line2Y, errorColor);
        }
        
        // Restore original font size for other UI elements
        m_textRenderer->setFontSize(100);
    } else if (!m_errorMessage.empty()) {
        // Clear expired error message
        m_errorMessage.clear();
    }
    
    // Render page jump input if active
    if (m_pageJumpInputActive) {
        // Check for timeout
        if (SDL_GetTicks() - m_pageJumpStartTime > PAGE_JUMP_TIMEOUT) {
            const_cast<App*>(this)->cancelPageJumpInput();
        } else {
            SDL_Color jumpColor = {255, 255, 255, 255}; // White text
            SDL_Color jumpBgColor = {0, 100, 200, 200}; // Semi-transparent blue background
            
            // Use larger font for page jump input
            int jumpFontScale = 300; // 300% = 3x larger 
            m_textRenderer->setFontSize(jumpFontScale);
            
            // Calculate actual font size for positioning
            int actualFontSize = static_cast<int>(baseFontSize * (jumpFontScale / 100.0));
            
            std::string jumpPrompt = "Go to page: " + m_pageJumpBuffer + "_";
            std::string jumpHint = "Enter page number (1-" + std::to_string(m_pageCount) + "), press Enter to confirm, Esc to cancel";
            
            // Calculate positioning
            int avgCharWidth = actualFontSize * 0.6;
            int promptWidth = static_cast<int>(jumpPrompt.length()) * avgCharWidth;
            int hintWidth = static_cast<int>(jumpHint.length()) * (actualFontSize / 2); // Smaller font for hint
            
            int promptX = (currentWindowWidth - promptWidth) / 2;
            int promptY = (currentWindowHeight - actualFontSize * 2) / 2;
            
            // Draw background rectangle
            int bgWidth = std::max(promptWidth, hintWidth) + 40;
            int bgHeight = actualFontSize * 3;
            SDL_Rect bgRect = {promptX - 20, promptY - 10, bgWidth, bgHeight};
            SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), jumpBgColor.r, jumpBgColor.g, jumpBgColor.b, jumpBgColor.a);
            SDL_SetRenderDrawBlendMode(m_renderer->getSDLRenderer(), SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(m_renderer->getSDLRenderer(), &bgRect);
            
            // Draw prompt text
            m_textRenderer->renderText(jumpPrompt, promptX, promptY, jumpColor);
            
            // Draw hint text (smaller)
            m_textRenderer->setFontSize(150); // 150% for hint
            int hintX = (currentWindowWidth - hintWidth) / 2;
            int hintY = promptY + actualFontSize + 10;
            m_textRenderer->renderText(jumpHint, hintX, hintY, jumpColor);
            
            // Restore original font size
            m_textRenderer->setFontSize(100);
        }
    }
}

void App::goToNextPage()
{
    if (m_currentPage < m_pageCount - 1)
    {
        m_currentPage++;
        onPageChangedKeepZoom();
        alignToTopOfCurrentPage();
        updateScaleDisplayTime();
        updatePageDisplayTime();
        markDirty();
        
        // Set cooldown timer to prevent rapid page changes during panning
        m_lastPageChangeTime = SDL_GetTicks();
        
        // Request preloading of upcoming pages
        if (m_pagePreloader) {
            m_pagePreloader->requestPreload(m_currentPage, m_currentScale);
        }
    }
}

void App::goToPreviousPage()
{
    if (m_currentPage > 0)
    {
        m_currentPage--;
        onPageChangedKeepZoom();
        alignToTopOfCurrentPage();
        updateScaleDisplayTime();
        updatePageDisplayTime();
        markDirty();
        
        // Set cooldown timer to prevent rapid page changes during panning
        m_lastPageChangeTime = SDL_GetTicks();
        
        // Request preloading of upcoming pages
        if (m_pagePreloader) {
            m_pagePreloader->requestPreload(m_currentPage, m_currentScale);
        }
    }
}

void App::goToPage(int pageNum)
{
    if (pageNum >= 0 && pageNum < m_pageCount)
    {
        m_currentPage = pageNum;
        onPageChangedKeepZoom();
        alignToTopOfCurrentPage();
        updateScaleDisplayTime();
        updatePageDisplayTime();
        markDirty();
        
        // Request preloading of upcoming pages
        if (m_pagePreloader) {
            m_pagePreloader->requestPreload(m_currentPage, m_currentScale);
        }
    }
}

void App::zoom(int delta)
{
    // Throttle zoom operations to prevent rapid cache clearing and bus errors
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastZoomTime).count();
    if (elapsed < ZOOM_THROTTLE_MS) {
        return; // Too soon, ignore this zoom request
    }
    m_lastZoomTime = now;

    int oldScale = m_currentScale;
    m_currentScale += delta;
    if (m_currentScale < 10)
        m_currentScale = 10;
    if (m_currentScale > 350)
        m_currentScale = 350;

    recenterScrollOnZoom(oldScale, m_currentScale);
    clampScroll();
    updateScaleDisplayTime();
    updatePageDisplayTime();
    markDirty();
    
    // Clear cache and request bidirectional preloading at new scale
    if (m_pagePreloader && oldScale != m_currentScale) {
        m_pagePreloader->clearCache();
        m_pagePreloader->requestBidirectionalPreload(m_currentPage, m_currentScale);
    }
}

void App::zoomTo(int scale)
{
    // Throttle zoom operations to prevent rapid cache clearing and bus errors
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastZoomTime).count();
    if (elapsed < ZOOM_THROTTLE_MS) {
        return; // Too soon, ignore this zoom request
    }
    m_lastZoomTime = now;

    int oldScale = m_currentScale;
    m_currentScale = scale;
    if (m_currentScale < 10)
        m_currentScale = 10;
    if (m_currentScale > 350)
        m_currentScale = 350;

    recenterScrollOnZoom(oldScale, m_currentScale);
    clampScroll();
    updateScaleDisplayTime();
    updatePageDisplayTime();
    markDirty();
    
    // Clear cache and request bidirectional preloading at new scale
    if (m_pagePreloader && oldScale != m_currentScale) {
        m_pagePreloader->clearCache();
        m_pagePreloader->requestBidirectionalPreload(m_currentPage, m_currentScale);
    }
}

void App::fitPageToWindow()
{
    int oldScale = m_currentScale; // Track old scale for preloader
    
    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

    // Update max render size for downsampling
    if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get()))
    {
        muDoc->setMaxRenderSize(windowWidth, windowHeight);
    }

    // Use effective sizes so 90/270 rotation swaps W/H
    int nativeWidth = effectiveNativeWidth();
    int nativeHeight = effectiveNativeHeight();

    if (nativeWidth == 0 || nativeHeight == 0)
    {
        std::cerr << "App ERROR: Native page dimensions are zero for page "
                  << m_currentPage << std::endl;
        return;
    }

    int scaleToFitWidth = static_cast<int>((static_cast<double>(windowWidth) / nativeWidth) * 100.0);
    int scaleToFitHeight = static_cast<int>((static_cast<double>(windowHeight) / nativeHeight) * 100.0);

    m_currentScale = std::min(scaleToFitWidth, scaleToFitHeight);
    if (m_currentScale < 10)
        m_currentScale = 10;
    if (m_currentScale > 350)
        m_currentScale = 350;

    m_pageWidth = static_cast<int>(nativeWidth * (m_currentScale / 100.0));
    m_pageHeight = static_cast<int>(nativeHeight * (m_currentScale / 100.0));

    m_scrollX = 0;
    m_scrollY = 0;
    updateScaleDisplayTime();
    updatePageDisplayTime();
    markDirty();
    
    // Clear cache and request bidirectional preloading at new scale
    if (m_pagePreloader && oldScale != m_currentScale) {
        m_pagePreloader->clearCache();
        m_pagePreloader->requestBidirectionalPreload(m_currentPage, m_currentScale);
    }
}

void App::recenterScrollOnZoom(int oldScale, int newScale)
{
    if (oldScale == 0 || newScale == 0)
        return;

    int nativeWidth = effectiveNativeWidth();
    int nativeHeight = effectiveNativeHeight();

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

void App::clampScroll()
{
    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

    int maxScrollX = std::max(0, (m_pageWidth - windowWidth) / 2);
    int maxScrollY = std::max(0, (m_pageHeight - windowHeight) / 2);

    m_scrollX = std::max(-maxScrollX, std::min(maxScrollX, m_scrollX));
    m_scrollY = std::max(-maxScrollY, std::min(maxScrollY, m_scrollY));
}

void App::resetPageView()
{
    m_currentPage = 0;
    m_currentScale = 100;
    fitPageToWindow();
}

// ---- helpers  ----
void App::jumpPages(int delta)
{
    int target = std::clamp(m_currentPage + delta, 0, m_pageCount - 1);
    goToPage(target);
}

void App::rotateClockwise()
{
    m_rotation = (m_rotation + 90) % 360;
    onPageChangedKeepZoom();
    alignToTopOfCurrentPage();
    markDirty();
}

void App::toggleMirrorVertical()
{
    m_mirrorV = !m_mirrorV;
    markDirty();
}

void App::toggleMirrorHorizontal()
{
    m_mirrorH = !m_mirrorH;
    markDirty();
}

void App::fitPageToWidth()
{
    int oldScale = m_currentScale; // Track old scale for preloader
    
    int windowWidth = m_renderer->getWindowWidth();

    // Use effective sizes so 90/270 rotation swaps W/H
    int nativeWidth = effectiveNativeWidth();

    if (nativeWidth == 0)
    {
        std::cerr << "App ERROR: Native page width is zero for page "
                  << m_currentPage << std::endl;
        return;
    }

    // Calculate scale to fit width with a small margin (95% of window width)
    // This accounts for potential downsampling and provides better visual fit
    double targetWidth = windowWidth * 0.95; // 5% margin
    m_currentScale = static_cast<int>((targetWidth / nativeWidth) * 100.0);
    
    // Clamp scale to reasonable bounds
    if (m_currentScale < 10)
        m_currentScale = 10;
    if (m_currentScale > 350)
        m_currentScale = 350;

    // Update page dimensions based on new scale
    int nativeHeight = effectiveNativeHeight();
    m_pageWidth = static_cast<int>(nativeWidth * (m_currentScale / 100.0));
    m_pageHeight = static_cast<int>(nativeHeight * (m_currentScale / 100.0));

    // Reset horizontal scroll since we're fitting to width
    m_scrollX = 0;
    
    // For vertical scroll, if the page is taller than window, start at top
    int windowHeight = m_renderer->getWindowHeight();
    if (m_pageHeight > windowHeight)
    {
        // Start at top of page (positive maxY in your coordinate system)
        int maxY = (m_pageHeight - windowHeight) / 2;
        m_scrollY = maxY;
    }
    else
    {
        // Page fits vertically, center it
        m_scrollY = 0;
    }
    
    clampScroll();
    updateScaleDisplayTime();
    updatePageDisplayTime();
    markDirty();
    
    // Clear cache and request bidirectional preloading at new scale
    if (m_pagePreloader && oldScale != m_currentScale) {
        m_pagePreloader->clearCache();
        m_pagePreloader->requestBidirectionalPreload(m_currentPage, m_currentScale);
    }
}

void App::printAppState()
{
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

void App::initializeGameControllers()
{
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        if (SDL_IsGameController(i))
        {
            m_gameController = SDL_GameControllerOpen(i);
            if (m_gameController)
            {
                m_gameControllerInstanceID = SDL_JoystickGetDeviceInstanceID(i);
                std::cout << "Opened game controller: " << SDL_GameControllerName(m_gameController) << std::endl;
                break;
            }
            else
            {
                std::cerr << "Could not open game controller: " << SDL_GetError() << std::endl;
            }
        }
    }
}

void App::closeGameControllers()
{
    if (m_gameController)
    {
        SDL_GameControllerClose(m_gameController);
        m_gameController = nullptr;
        m_gameControllerInstanceID = -1;
        std::cout << "Closed game controller." << std::endl;
    }
}

bool App::updateHeldPanning(float dt)
{
    bool changed = false;
    float dx = 0.0f, dy = 0.0f;

    if (m_dpadLeftHeld) {
        dx += 1.0f;
    }
    if (m_dpadRightHeld) {
        dx -= 1.0f;
    }
    if (m_dpadUpHeld) {
        dy += 1.0f;
    }
    if (m_dpadDownHeld) {
        dy -= 1.0f;
    }

    // Check if we're in scroll timeout after a page change
    bool inScrollTimeout = isInScrollTimeout();
    
    if (dx != 0.0f || dy != 0.0f)
    {
        if (inScrollTimeout) {
            // During scroll timeout, don't allow panning movement
            // This prevents scrolling past the beginning of a new page
            // But we still need to continue processing edge-turn logic below
        } else {
            float len = std::sqrt(dx * dx + dy * dy);
            dx /= len;
            dy /= len;

            int oldScrollX = m_scrollX;
            int oldScrollY = m_scrollY;
            
            float moveX = dx * m_dpadPanSpeed * dt;
            float moveY = dy * m_dpadPanSpeed * dt;
            
            // Ensure minimum movement of 1 pixel if there's any input
            int pixelMoveX = static_cast<int>(moveX);
            int pixelMoveY = static_cast<int>(moveY);
            if (dx != 0.0f && pixelMoveX == 0) {
                pixelMoveX = (dx > 0) ? 1 : -1;
            }
            if (dy != 0.0f && pixelMoveY == 0) {
                pixelMoveY = (dy > 0) ? 1 : -1;
            }
            
            m_scrollX += pixelMoveX;
            m_scrollY += pixelMoveY;
            clampScroll();
            
            if (m_scrollX != oldScrollX || m_scrollY != oldScrollY) {
                changed = true;
            }
        }
    }

    // --- HORIZONTAL edge → page turn ---
    const int maxX = getMaxScrollX();

    // Reset edge-turn timers during scroll timeout to prevent accumulated time from previous page
    if (inScrollTimeout) {
        m_edgeTurnHoldRight = 0.0f;
        m_edgeTurnHoldLeft = 0.0f;
        m_edgeTurnHoldUp = 0.0f;
        m_edgeTurnHoldDown = 0.0f;
    } else {
        // Only accumulate edge-turn time when not in scroll timeout
        if (maxX == 0)
        {
            if (m_dpadRightHeld)
                m_edgeTurnHoldRight += dt;
            else
                m_edgeTurnHoldRight = 0.0f;
            if (m_dpadLeftHeld)
                m_edgeTurnHoldLeft += dt;
            else
                m_edgeTurnHoldLeft = 0.0f;
        }
        else
        {
            if (m_scrollX == -maxX && m_dpadRightHeld)
                m_edgeTurnHoldRight += dt;
            else
                m_edgeTurnHoldRight = 0.0f;
            if (m_scrollX == maxX && m_dpadLeftHeld)
                m_edgeTurnHoldLeft += dt;
            else
                m_edgeTurnHoldLeft = 0.0f;
        }
    }

    if (m_edgeTurnHoldRight >= m_edgeTurnThreshold)
    {
        if (m_currentPage < m_pageCount - 1 && !isInPageChangeCooldown())
        {
            goToNextPage();
            m_scrollX = getMaxScrollX(); // appear at left edge
            clampScroll();
            changed = true;
        }
        m_edgeTurnHoldRight = 0.0f;
    }
    else if (m_edgeTurnHoldLeft >= m_edgeTurnThreshold)
    {
        if (m_currentPage > 0 && !isInPageChangeCooldown())
        {
            goToPreviousPage();
            m_scrollX = -getMaxScrollX(); // appear at right edge
            clampScroll();
            changed = true;
        }
        m_edgeTurnHoldLeft = 0.0f;
    }

    // --- VERTICAL edge → page turn (NEW) ---
    const int maxY = getMaxScrollY();

    if (!inScrollTimeout) {
        // Only accumulate edge-turn time when not in scroll timeout
        if (maxY == 0)
        {
            // Page fits vertically: treat sustained up/down as page turns
            if (m_dpadDownHeld)
                m_edgeTurnHoldDown += dt;
            else
                m_edgeTurnHoldDown = 0.0f;
            if (m_dpadUpHeld)
                m_edgeTurnHoldUp += dt;
            else
                m_edgeTurnHoldUp = 0.0f;
        }
        else
        {
            // Bottom edge & still pushing down? (down moves view further down in your scheme: dy < 0)
            if (m_scrollY == -maxY && m_dpadDownHeld)
                m_edgeTurnHoldDown += dt;
            else
                m_edgeTurnHoldDown = 0.0f;

            // Top edge & still pushing up?
            if (m_scrollY == maxY && m_dpadUpHeld)
                m_edgeTurnHoldUp += dt;
            else
                m_edgeTurnHoldUp = 0.0f;
        }
    }

    if (m_edgeTurnHoldDown >= m_edgeTurnThreshold)
    {
        if (m_currentPage < m_pageCount - 1 && !isInPageChangeCooldown())
        {
            goToNextPage();
            // Land at the top edge of the new page so motion feels continuous downward
            m_scrollY = getMaxScrollY();
            clampScroll();
            changed = true;
        }
        m_edgeTurnHoldDown = 0.0f;
    }
    else if (m_edgeTurnHoldUp >= m_edgeTurnThreshold)
    {
        if (m_currentPage > 0 && !isInPageChangeCooldown())
        {
            goToPreviousPage();
            // Land at the bottom edge of the previous page
            m_scrollY = -getMaxScrollY();
            clampScroll();
            changed = true;
        }
        m_edgeTurnHoldUp = 0.0f;
    }
    
    return changed;
}

int App::getMaxScrollX() const
{
    int windowWidth = m_renderer->getWindowWidth();
    return std::max(0, (m_pageWidth - windowWidth) / 2);
}
int App::getMaxScrollY() const
{
    int windowHeight = m_renderer->getWindowHeight();
    return std::max(0, (m_pageHeight - windowHeight) / 2);
}

void App::handleDpadNudgeRight()
{
    const int maxX = getMaxScrollX();
    // Right nudge while already at right edge -> next page
    if (maxX == 0 || m_scrollX == -maxX)
    {
        if (m_currentPage < m_pageCount - 1 && !isInPageChangeCooldown())
        {
            goToNextPage();
            m_scrollX = getMaxScrollX(); // appear at left edge of new page
            clampScroll();
        }
        return;
    }
    m_scrollX -= 50;
    clampScroll();
}

void App::handleDpadNudgeLeft()
{
    const int maxX = getMaxScrollX();
    // Left nudge while already at left edge -> previous page
    if (maxX == 0 || m_scrollX == maxX)
    {
        if (m_currentPage > 0 && !isInPageChangeCooldown())
        {
            goToPreviousPage();
            m_scrollX = -getMaxScrollX(); // appear at right edge of prev page
            clampScroll();
        }
        return;
    }
    m_scrollX += 50;
    clampScroll();
}

void App::handleDpadNudgeDown()
{
    const int maxY = getMaxScrollY();
    // Down nudge while already at bottom edge -> next page
    if (maxY == 0 || m_scrollY == -maxY)
    {
        if (m_currentPage < m_pageCount - 1 && !isInPageChangeCooldown())
        {
            goToNextPage();
            m_scrollY = getMaxScrollY(); // appear at top edge of new page
            clampScroll();
        }
        return;
    }
    m_scrollY -= 50;
    clampScroll();
}

void App::handleDpadNudgeUp()
{
    const int maxY = getMaxScrollY();
    // Up nudge while already at top edge -> previous page
    if (maxY == 0 || m_scrollY == maxY)
    {
        if (m_currentPage > 0 && !isInPageChangeCooldown())
        {
            goToPreviousPage();
            m_scrollY = -getMaxScrollY(); // appear at bottom edge of prev page
            clampScroll();
        }
        return;
    }
    m_scrollY += 50;
    clampScroll();
}

void App::onPageChangedKeepZoom()
{
    // Predict scaled size for the new page using the current zoom
    int nativeW = effectiveNativeWidth();
    int nativeH = effectiveNativeHeight();

    // Guard against bad docs
    if (nativeW <= 0 || nativeH <= 0)
    {
        std::cerr << "App ERROR: Native page dimensions are zero for page " << m_currentPage << std::endl;
        return;
    }

    m_pageWidth = static_cast<int>(nativeW * (m_currentScale / 100.0));
    m_pageHeight = static_cast<int>(nativeH * (m_currentScale / 100.0));

    // Keep current scroll but ensure it's valid for the new page extents
    clampScroll();
}

void App::alignToTopOfCurrentPage()
{
    // Recompute extents with current zoom (safe even if already set)
    // Use effective size for MuPdfDocument to account for downsampling
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
    int effectiveW, effectiveH;
    
    if (muPdfDoc) {
        effectiveW = muPdfDoc->getPageWidthEffective(m_currentPage, m_currentScale);
        effectiveH = muPdfDoc->getPageHeightEffective(m_currentPage, m_currentScale);
        
        // Apply rotation
        if (m_rotation % 180 != 0) {
            std::swap(effectiveW, effectiveH);
        }
    } else {
        // Fallback for other document types
        int nativeW = effectiveNativeWidth();
        int nativeH = effectiveNativeHeight();
        effectiveW = static_cast<int>(nativeW * (m_currentScale / 100.0));
        effectiveH = static_cast<int>(nativeH * (m_currentScale / 100.0));
    }
    
    if (effectiveW <= 0 || effectiveH <= 0)
        return;

    m_pageWidth = effectiveW;
    m_pageHeight = effectiveH;

    // If the page is taller than the window, place the view at the *top edge*
    // With your clamp scheme, "top edge" corresponds to +maxY
    int windowH = m_renderer->getWindowHeight();
    int maxY = std::max(0, (m_pageHeight - windowH) / 2);

    if (maxY > 0)
    {
        m_scrollY = +maxY; // top edge visible
        clampScroll();
    }
    else
    {
        // No vertical scroll range (fits): request a top-aligned render once
        m_forceTopAlignNextRender = true;
    }
}

int App::effectiveNativeWidth() const
{
    int w = m_document->getPageWidthNative(m_currentPage);
    int h = m_document->getPageHeightNative(m_currentPage);
    return (m_rotation % 180 == 0) ? w : h;
}

int App::effectiveNativeHeight() const
{
    int w = m_document->getPageWidthNative(m_currentPage);
    int h = m_document->getPageHeightNative(m_currentPage);
    return (m_rotation % 180 == 0) ? h : w;
}

SDL_RendererFlip App::currentFlipFlags() const
{
    SDL_RendererFlip f = SDL_FLIP_NONE;
    if (m_mirrorH)
        f = (SDL_RendererFlip)(f | SDL_FLIP_HORIZONTAL);
    if (m_mirrorV)
        f = (SDL_RendererFlip)(f | SDL_FLIP_VERTICAL);
    return f;
}

void App::showErrorMessage(const std::string& message)
{
    m_errorMessage = message;
    m_errorMessageTime = SDL_GetTicks();
}

void App::updateScaleDisplayTime()
{
    m_scaleDisplayTime = SDL_GetTicks();
}

void App::updatePageDisplayTime()
{
    m_pageDisplayTime = SDL_GetTicks();
}

void App::startPageJumpInput()
{
    m_pageJumpInputActive = true;
    m_pageJumpBuffer.clear();
    m_pageJumpStartTime = SDL_GetTicks();
    std::cout << "Page jump mode activated. Enter page number (1-" << m_pageCount << ") and press Enter." << std::endl;
}

void App::handlePageJumpInput(char digit)
{
    if (!m_pageJumpInputActive) return;
    
    // Check if we're still within timeout
    if (SDL_GetTicks() - m_pageJumpStartTime > PAGE_JUMP_TIMEOUT) {
        cancelPageJumpInput();
        return;
    }
    
    // Limit input length to prevent overflow
    if (m_pageJumpBuffer.length() < 10) {
        m_pageJumpBuffer += digit;
        std::cout << "Page jump input: " << m_pageJumpBuffer << std::endl;
    }
}

void App::cancelPageJumpInput()
{
    if (m_pageJumpInputActive) {
        m_pageJumpInputActive = false;
        m_pageJumpBuffer.clear();
        std::cout << "Page jump cancelled." << std::endl;
    }
}

void App::confirmPageJumpInput()
{
    if (!m_pageJumpInputActive) return;
    
    if (m_pageJumpBuffer.empty()) {
        cancelPageJumpInput();
        return;
    }
    
    try {
        int targetPage = std::stoi(m_pageJumpBuffer);
        
        // Convert from 1-based to 0-based indexing
        targetPage -= 1;
        
        if (targetPage >= 0 && targetPage < m_pageCount) {
            goToPage(targetPage);
            std::cout << "Jumped to page " << (targetPage + 1) << std::endl;
        } else {
            std::cout << "Invalid page number. Valid range: 1-" << m_pageCount << std::endl;
            showErrorMessage("Invalid page: " + m_pageJumpBuffer + ". Valid range: 1-" + std::to_string(m_pageCount));
        }
    } catch (const std::exception& e) {
        std::cout << "Invalid page number format: " << m_pageJumpBuffer << std::endl;
        showErrorMessage("Invalid page number: " + m_pageJumpBuffer);
    }
    
    m_pageJumpInputActive = false;
    m_pageJumpBuffer.clear();
}
