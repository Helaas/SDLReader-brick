#include "app.h"
#include "renderer.h"
#include "text_renderer.h"
#include "document.h"
#include "mupdf_document.h"
#include "gui_manager.h"
#include "options_manager.h"
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
    : m_running(true), m_currentPage(0)
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
            markDirty(); // Force screen redraw to show black screen
        } else {
            std::cout << "App: Exiting fake sleep mode - re-enabling inputs and screen" << std::endl;
            markDirty(); // Force screen redraw to restore normal display
        }
    });
#endif

    // Initialize font manager FIRST, before document creation
    m_optionsManager = std::make_unique<OptionsManager>();
    
    // Initialize viewport manager
    m_viewportManager = std::make_unique<ViewportManager>(m_renderer.get());
    
    // Load saved font configuration early
    FontConfig savedConfig = m_optionsManager->loadConfig();

    // Determine document type based on file extension
    // MuPDF supports PDF, CBZ, ZIP (with images), XPS, EPUB, and other formats
    std::string lowercaseFilename = filename;
    std::transform(lowercaseFilename.begin(), lowercaseFilename.end(), 
                   lowercaseFilename.begin(), ::tolower);
    
    // MuPDF can handle all these formats through its generic document interface
    if ((lowercaseFilename.size() >= 4 && 
         (lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".pdf" ||
          lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".cbz" ||
          lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".cbr" ||
          lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".rar" ||
          lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".zip")) ||
        (lowercaseFilename.size() >= 5 && 
         (lowercaseFilename.substr(lowercaseFilename.size() - 5) == ".epub" ||
          lowercaseFilename.substr(lowercaseFilename.size() - 5) == ".mobi"))) {
        m_document = std::make_unique<MuPdfDocument>();
        
        // Apply saved CSS configuration BEFORE opening document
        if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get())) {
            if (!savedConfig.fontPath.empty()) {
                std::string css = m_optionsManager->generateCSS(savedConfig);
                if (!css.empty()) {
                    muDoc->setUserCSSBeforeOpen(css);
                    std::cout << "Applied saved font CSS before opening document: " << savedConfig.fontName << std::endl;
                }
            }
        }
    }
    else
    {
        throw std::runtime_error("Unsupported file format: " + filename + 
                                " (supported: .pdf, .cbz, .cbr, .rar, .zip, .epub, .mobi)");
    }

    if (!m_document->open(filename))
    {
        throw std::runtime_error("Failed to open document: " + filename);
    }

#ifndef TG5040_PLATFORM
// Set max render size for downsampling - allow for meaningful zoom levels on non-TG5040 platforms
// Use 4x window size to enable proper zooming while TG5040 has no limit
if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get()))
{
    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();
    // Allow 4x zoom by setting max render size to 4x window size
    muDoc->setMaxRenderSize(windowWidth * 4, windowHeight * 4);
}
#endif

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

    // Initialize InputManager
    m_inputManager = std::make_unique<InputManager>();
    m_inputManager->setZoomStep(m_optionsManager->loadConfig().zoomStep);
    m_inputManager->setPageCount(m_pageCount);

    // Install custom font loader for MuPDF if this is a MuPDF document
    if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get())) {
        m_optionsManager->installFontLoader(muDoc->getContext());
        std::cout << "DEBUG: Custom font loader installed for MuPDF document" << std::endl;
    }
    
    // Initialize GUI manager AFTER font manager
    m_guiManager = std::make_unique<GuiManager>();
    if (!m_guiManager->initialize(window, renderer)) {
        throw std::runtime_error("Failed to initialize GUI manager");
    }

    // Set up font apply callback AFTER all initialization is complete
    m_guiManager->setFontApplyCallback([this](const FontConfig& config) {
        std::cout << "DEBUG: Font apply callback triggered" << std::endl;
        applyFontConfiguration(config);
    });

    // Set up font close callback to trigger redraw
    m_guiManager->setFontCloseCallback([this]() {
        markDirty(); // Force redraw to clear menu
    });

    // Set up page jump callback
    m_guiManager->setPageJumpCallback([this](int pageNumber) {
        std::cout << "DEBUG: Page jump callback triggered to page " << (pageNumber + 1) << std::endl;
        goToPage(pageNumber);
    });

    // Initialize page information in GUI manager
    m_guiManager->setPageCount(m_pageCount);
    m_guiManager->setCurrentPage(m_currentPage);

    // Now set the saved configuration in GUI if it exists
    if (!savedConfig.fontPath.empty()) {
        m_guiManager->setCurrentFontConfig(savedConfig);
        std::cout << "Applied saved font configuration: " << savedConfig.fontName << " at " << savedConfig.fontSize << "pt" << std::endl;
    }
}

App::~App()
{
#ifdef TG5040_PLATFORM
    if (m_powerHandler) {
        m_powerHandler->stop();
    }
#endif
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
        // Always start ImGui frame at the beginning of each main loop iteration
        // This ensures proper frame lifecycle management
        if (m_guiManager && !m_inFakeSleep) {
            m_guiManager->newFrame();
        }
        
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
            
            // Check for settled zoom input and apply pending zoom through ViewportManager
            if (!m_viewportManager->isZoomDebouncing()) {
                // Zoom input has settled, apply the final accumulated zoom
                m_viewportManager->applyPendingZoom(m_document.get(), m_currentPage);
                markDirty();
            }
            
            // Apply pending font changes safely in the main loop
            if (m_pendingFontChange) {
                applyPendingFontChange();
            }
            
            // Enhanced frame pacing for TG5040: Skip rendering if we're rendering too frequently
            // This helps prevent warping during rapid input changes
            static Uint32 lastRenderTime = 0;
            Uint32 currentTime = SDL_GetTicks();
            
            // Force rendering if the font menu is visible, otherwise use normal logic
            bool shouldRender = false;
            if (m_guiManager && m_guiManager->isFontMenuVisible()) {
                shouldRender = true; // Always render when font menu is visible
            } else {
                // Force render if marked dirty (e.g., after menu close) or other conditions
                shouldRender = m_needsRedraw || panningChanged || 
                              ((currentTime - lastRenderTime) >= 16); // More aggressive rendering
            }
            
            bool doRender = false;
            
            if (m_viewportManager->isZoomDebouncing()) {
                // During zoom processing, show processing indicator with minimal rendering
                // Re-render the current page at current scale with indicator overlay
                if ((currentTime - lastRenderTime) >= 100) { // Even slower update rate to minimize flicker
                    doRender = true;
                    // Important: Don't reset m_needsRedraw - preserve for final zoom render
                }
            }
            else if (shouldRender) {
                // Normal rendering when not processing zoom
                doRender = true;
            }
            
            if (doRender) {
                renderCurrentPage();
                renderUI();
                
                // Always render ImGui if we started a frame (which we always do when not in fake sleep)
                if (m_guiManager) {
                    m_guiManager->render();
                }
                
                m_renderer->present();
                lastRenderTime = currentTime;
                
                // Only reset needsRedraw for normal rendering, not during zoom debouncing
                if (!m_viewportManager->isZoomDebouncing()) {
                    m_needsRedraw = false;
                }
            } else {
                // Even if we don't render the main content, we must still finish the ImGui frame
                // to maintain proper frame lifecycle
                if (m_guiManager) {
                    m_guiManager->render();
                }
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
    // Let ImGui handle the event first
    if (m_guiManager) {
        m_guiManager->handleEvent(event);
    }
    
    // Block ALL input events if the settings menu is visible, except ESC to close it
    if (m_guiManager && (m_guiManager->isFontMenuVisible() || m_guiManager->isNumberPadVisible())) {
        // Always allow ESC and Q to close the menu
        if (event.type == SDL_KEYDOWN && 
            (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q)) {
            if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                m_guiManager->toggleFontMenu();
                markDirty();
            }
        }
        // Always allow quit events
        else if (event.type == SDL_QUIT) {
            m_running = false;
        }
        // Block everything else when menu or number pad is visible to prevent bleeding through
        return;
    }

    // Process input through InputManager
    InputActionData actionData = m_inputManager->processEvent(event);
    processInputAction(actionData);

    // Update App's input state variables for held button tracking
    // This is needed for updateHeldPanning() and edge-turn logic
    updateInputState(event);
}

void App::processInputAction(const InputActionData& actionData)
{
    switch (actionData.action) {
    case InputAction::Quit:
        m_running = false;
        break;
    case InputAction::Resize:
        m_viewportManager->fitPageToWindow(m_document.get(), m_currentPage);
        markDirty();
        break;
    case InputAction::ToggleFontMenu:
        toggleFontMenu();
        break;
    case InputAction::GoToNextPage:
        if (!isInPageChangeCooldown()) {
            goToNextPage();
        }
        break;
    case InputAction::GoToPreviousPage:
        if (!isInPageChangeCooldown()) {
            goToPreviousPage();
        }
        break;
    case InputAction::ZoomIn:
        m_viewportManager->zoom(m_optionsManager->loadConfig().zoomStep, m_document.get());
        markDirty();
        break;
    case InputAction::ZoomOut:
        m_viewportManager->zoom(-m_optionsManager->loadConfig().zoomStep, m_document.get());
        markDirty();
        break;
    case InputAction::ZoomTo:
        m_viewportManager->zoomTo(actionData.intValue > 0 ? actionData.intValue : 100, m_document.get());
        markDirty();
        break;
    case InputAction::GoToFirstPage:
        goToPage(0);
        break;
    case InputAction::GoToLastPage:
        goToPage(m_pageCount - 1);
        break;
    case InputAction::GoToPage:
        if (actionData.intValue >= 0 && actionData.intValue < m_pageCount) {
            goToPage(actionData.intValue);
        }
        break;
    case InputAction::JumpPages:
        if (!isInPageChangeCooldown()) {
            jumpPages(actionData.intValue);
        }
        break;
    case InputAction::ToggleFullscreen:
        m_renderer->toggleFullscreen();
        m_viewportManager->fitPageToWindow(m_document.get(), m_currentPage);
        markDirty();
        break;
    case InputAction::StartPageJumpInput:
        startPageJumpInput();
        break;
    case InputAction::PrintAppState:
        printAppState();
        break;
    case InputAction::ClampScroll:
        m_viewportManager->clampScroll();
        break;
    case InputAction::FitPageToWidth:
        m_viewportManager->fitPageToWidth(m_document.get(), m_currentPage);
        markDirty();
        break;
    case InputAction::FitPageToWindow:
        m_viewportManager->fitPageToWindow(m_document.get(), m_currentPage);
        markDirty();
        break;
    case InputAction::ResetPageView:
        m_viewportManager->resetPageView(m_document.get());
        m_currentPage = 0; // Reset to first page
        markDirty();
        break;
    case InputAction::ToggleMirrorHorizontal:
        m_viewportManager->toggleMirrorHorizontal();
        markDirty();
        break;
    case InputAction::ToggleMirrorVertical:
        m_viewportManager->toggleMirrorVertical();
        markDirty();
        break;
    case InputAction::RotateClockwise:
        m_viewportManager->rotateClockwise();
        markDirty();
        break;
    case InputAction::ScrollUp:
        if (!isInScrollTimeout()) {
            m_viewportManager->setScrollY(m_viewportManager->getScrollY() + static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::ScrollDown:
        if (!isInScrollTimeout()) {
            m_viewportManager->setScrollY(m_viewportManager->getScrollY() - static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::MoveLeft:
        if (!isInScrollTimeout()) {
            m_viewportManager->setScrollX(m_viewportManager->getScrollX() + static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::MoveRight:
        if (!isInScrollTimeout()) {
            m_viewportManager->setScrollX(m_viewportManager->getScrollX() - static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::MoveUp:
        if (!isInScrollTimeout()) {
            m_viewportManager->setScrollY(m_viewportManager->getScrollY() + static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::MoveDown:
        if (!isInScrollTimeout()) {
            m_viewportManager->setScrollY(m_viewportManager->getScrollY() - static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::StartDragging:
        m_isDragging = true;
        m_lastTouchX = actionData.floatValue;
        m_lastTouchY = actionData.deltaX; // Using deltaX as second position value
        break;
    case InputAction::StopDragging:
        m_isDragging = false;
        break;
    case InputAction::UpdateDragging:
        if (m_isDragging && !isInScrollTimeout()) {
            float dx = actionData.floatValue - m_lastTouchX;
            float dy = actionData.deltaX - m_lastTouchY;
            m_viewportManager->setScrollX(m_viewportManager->getScrollX() + static_cast<int>(dx));
            m_viewportManager->setScrollY(m_viewportManager->getScrollY() + static_cast<int>(dy));
            m_lastTouchX = actionData.floatValue;
            m_lastTouchY = actionData.deltaX;
            m_viewportManager->clampScroll();
            updatePageDisplayTime();
            markDirty();
        }
        break;
    case InputAction::HandlePageJumpInput:
        if (m_pageJumpInputActive) {
            handlePageJumpInput(actionData.charValue);
        }
        break;
    case InputAction::ConfirmPageJumpInput:
        if (m_pageJumpInputActive) {
            confirmPageJumpInput();
        }
        break;
    case InputAction::CancelPageJumpInput:
        if (m_pageJumpInputActive) {
            cancelPageJumpInput();
        } else if (m_guiManager && m_guiManager->isFontMenuVisible()) {
            // Close font menu if it's open
            m_guiManager->toggleFontMenu();
            // Force redraw to clear the menu from screen
            markDirty();
        } else {
            m_running = false;
        }
        break;
    case InputAction::None:
    default:
        // No action to take
        break;
    }
}

void App::updateInputState(const SDL_Event& event)
{
    switch (event.type) {
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym) {
        case SDLK_RIGHT:
            if (!m_keyboardRightHeld) { // Only on true initial press
                m_keyboardRightHeld = true;
                if (!isInScrollTimeout()) {
                    handleDpadNudgeRight();
                    updatePageDisplayTime();
                    markDirty();
                }
            }
            break;
        case SDLK_LEFT:
            if (!m_keyboardLeftHeld) { // Only on true initial press
                m_keyboardLeftHeld = true;
                if (!isInScrollTimeout()) {
                    handleDpadNudgeLeft();
                    updatePageDisplayTime();
                    markDirty();
                }
            }
            break;
        case SDLK_UP:
            if (!m_keyboardUpHeld) { // Only on true initial press
                m_keyboardUpHeld = true;
                if (!isInScrollTimeout()) {
                    handleDpadNudgeUp();
                    updatePageDisplayTime();
                    markDirty();
                }
            }
            break;
        case SDLK_DOWN:
            if (!m_keyboardDownHeld) { // Only on true initial press
                m_keyboardDownHeld = true;
                if (!isInScrollTimeout()) {
                    handleDpadNudgeDown();
                    updatePageDisplayTime();
                    markDirty();
                }
            }
            break;
        }
        break;
        
    case SDL_KEYUP:
        switch (event.key.keysym.sym) {
        case SDLK_RIGHT:
            m_keyboardRightHeld = false;
            if (m_edgeTurnHoldRight > 0.0f) {
                m_edgeTurnCooldownRight = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldRight = 0.0f;
            markDirty();
            break;
        case SDLK_LEFT:
            m_keyboardLeftHeld = false;
            if (m_edgeTurnHoldLeft > 0.0f) {
                m_edgeTurnCooldownLeft = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldLeft = 0.0f;
            markDirty();
            break;
        case SDLK_UP:
            m_keyboardUpHeld = false;
            if (m_edgeTurnHoldUp > 0.0f) {
                m_edgeTurnCooldownUp = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldUp = 0.0f;
            markDirty();
            break;
        case SDLK_DOWN:
            m_keyboardDownHeld = false;
            if (m_edgeTurnHoldDown > 0.0f) {
                m_edgeTurnCooldownDown = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldDown = 0.0f;
            markDirty();
            break;
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
        
    case SDL_CONTROLLERBUTTONDOWN:
        if (event.cbutton.which == m_gameControllerInstanceID) {
            switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                if (!m_dpadRightHeld) { // Only on true initial press
                    m_dpadRightHeld = true;
                    handleDpadNudgeRight();
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                if (!m_dpadLeftHeld) { // Only on true initial press
                    m_dpadLeftHeld = true;
                    handleDpadNudgeLeft();
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (!m_dpadUpHeld) { // Only on true initial press
                    m_dpadUpHeld = true;
                    handleDpadNudgeUp();
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (!m_dpadDownHeld) { // Only on true initial press
                    m_dpadDownHeld = true;
                    handleDpadNudgeDown();
                }
                break;
            }
        }
        break;
        
    case SDL_CONTROLLERBUTTONUP:
        if (event.cbutton.which == m_gameControllerInstanceID) {
            switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                m_dpadRightHeld = false;
                if (m_edgeTurnHoldRight > 0.0f) {
                    m_edgeTurnCooldownRight = SDL_GetTicks() / 1000.0f;
                }
                m_edgeTurnHoldRight = 0.0f;
                markDirty();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                m_dpadLeftHeld = false;
                if (m_edgeTurnHoldLeft > 0.0f) {
                    m_edgeTurnCooldownLeft = SDL_GetTicks() / 1000.0f;
                }
                m_edgeTurnHoldLeft = 0.0f;
                markDirty();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                m_dpadUpHeld = false;
                if (m_edgeTurnHoldUp > 0.0f) {
                    m_edgeTurnCooldownUp = SDL_GetTicks() / 1000.0f;
                }
                m_edgeTurnHoldUp = 0.0f;
                markDirty();
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                m_dpadDownHeld = false;
                if (m_edgeTurnHoldDown > 0.0f) {
                    m_edgeTurnCooldownDown = SDL_GetTicks() / 1000.0f;
                }
                m_edgeTurnHoldDown = 0.0f;
                markDirty();
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
}

bool App::isInPageChangeCooldown() const
{
    return (SDL_GetTicks() - m_lastPageChangeTime) < PAGE_CHANGE_COOLDOWN;
}

bool App::isInScrollTimeout() const
{
    return (SDL_GetTicks() - m_lastPageChangeTime) < (m_lastRenderDuration + 50);
}

void App::loadDocument()
{
    m_currentPage = 0;
    m_viewportManager->fitPageToWindow(m_document.get(), m_currentPage);
}

void App::renderCurrentPage()
{
    Uint32 renderStart = SDL_GetTicks();
    
    m_renderer->clear(255, 255, 255, 255);

    int winW = m_renderer->getWindowWidth();
    int winH = m_renderer->getWindowHeight();

    int srcW, srcH;
    std::vector<uint32_t> argbData;
    {
        // Lock the document mutex to ensure thread-safe access
        std::lock_guard<std::mutex> lock(m_documentMutex);
        
        // Try to use ARGB rendering for better performance
        auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
        if (muPdfDoc) {
            try {
                argbData = muPdfDoc->renderPageARGB(m_currentPage, srcW, srcH, m_viewportManager->getCurrentScale());
            } catch (const std::exception& e) {
                // Fallback to RGB rendering
                std::vector<uint8_t> rgbData = m_document->renderPage(m_currentPage, srcW, srcH, m_viewportManager->getCurrentScale());
                // Convert to ARGB manually
                argbData.resize(srcW * srcH);
                for (int i = 0; i < srcW * srcH; ++i) {
                    argbData[i] = rgb24_to_argb32(rgbData[i*3], rgbData[i*3+1], rgbData[i*3+2]);
                }
            }
        } else {
            // Fallback to RGB rendering for other document types
            std::vector<uint8_t> rgbData = m_document->renderPage(m_currentPage, srcW, srcH, m_viewportManager->getCurrentScale());
            // Convert to ARGB manually
            argbData.resize(srcW * srcH);
            for (int i = 0; i < srcW * srcH; ++i) {
                argbData[i] = rgb24_to_argb32(rgbData[i*3], rgbData[i*3+1], rgbData[i*3+2]);
            }
        }
    }

    // Ensure page dimensions are updated BEFORE calculating positions
    // This prevents warping when switching between pages with different aspect ratios
    int newPageWidth, newPageHeight;
    
    // displayed page size after rotation
    if (m_viewportManager->getRotation() % 180 == 0)
    {
        newPageWidth = srcW;
        newPageHeight = srcH;
    }
    else
    {
        newPageWidth = srcH;
        newPageHeight = srcW;
    }
    
    // Only update page dimensions if they've actually changed
    // This provides more stable rendering during rapid input
    if (m_viewportManager->getPageWidth() != newPageWidth || m_viewportManager->getPageHeight() != newPageHeight) {
        m_viewportManager->setPageDimensions(newPageWidth, newPageHeight);
        // Clamp scroll position when page dimensions change to prevent out-of-bounds rendering
        m_viewportManager->clampScroll();
    }

    int posX = (winW - m_viewportManager->getPageWidth()) / 2 + m_viewportManager->getScrollX();

    int posY;
    if (m_viewportManager->getPageHeight() <= winH)
    {
        const auto& state = m_viewportManager->getState();
        if (state.topAlignWhenFits || state.forceTopAlignNextRender)
            posY = 0;
        else
            posY = (winH - m_viewportManager->getPageHeight()) / 2;
    }
    else
    {
        posY = (winH - m_viewportManager->getPageHeight()) / 2 + m_viewportManager->getScrollY();
    }
    m_viewportManager->setForceTopAlignNextRender(false);

    m_renderer->renderPageExARGB(argbData, srcW, srcH,
                                 posX, posY, m_viewportManager->getPageWidth(), m_viewportManager->getPageHeight(),
                                 static_cast<double>(m_viewportManager->getRotation()),
                                 m_viewportManager->currentFlipFlags());
    
    // Trigger prerendering of adjacent pages for faster page changes
    // Do this after the main render to avoid blocking the current page display
    // Only prerender if zoom is stable (not debouncing), not actively panning, and not in cooldown
    static Uint32 lastPrerenderTrigger = 0;
    Uint32 currentTime = SDL_GetTicks();
    bool prerenderCooldownActive = (currentTime - lastPrerenderTrigger) < 200; // 200ms cooldown
    
    if (!m_viewportManager->isZoomDebouncing() && !m_isDragging && !prerenderCooldownActive) {
        auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
        if (muPdfDoc && !muPdfDoc->isPrerenderingActive()) {
            muPdfDoc->prerenderAdjacentPagesAsync(m_currentPage, m_viewportManager->getCurrentScale());
            lastPrerenderTrigger = currentTime;
        }
    }
    
    // Measure total render time for dynamic timeout
    m_lastRenderDuration = SDL_GetTicks() - renderStart;
}

void App::renderUI()
{
    int baseFontSize = 16;
    // setFontSize expects percentage scale, so 100% = normal base size
    m_textRenderer->setFontSize(100);

    SDL_Color textColor = {0, 0, 0, 255};
    std::string pageInfo = "Page: " + std::to_string(m_currentPage + 1) + "/" + std::to_string(m_pageCount);
    std::string scaleInfo = "Scale: " + std::to_string(m_viewportManager->getCurrentScale()) + "%";

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
    
    // Render zoom processing indicator
    if (m_viewportManager->shouldShowZoomProcessingIndicator()) {
        SDL_Color processingColor = {255, 255, 0, 255}; // Bright yellow text for high visibility
        SDL_Color processingBgColor = {0, 0, 0, 250}; // Nearly opaque background
        
        std::string processingText = "Processing zoom...";
        
        // Use larger font for better visibility during longer operations
        m_textRenderer->setFontSize(150); // Even larger for better visibility
        int avgCharWidth = 12; // Adjusted for larger font
        int textWidth = static_cast<int>(processingText.length()) * avgCharWidth;
        int textHeight = 24; // Adjusted for larger font
        
        // Position prominently in top-center
        int textX = (currentWindowWidth - textWidth) / 2;
        int textY = 20;
        
        // Draw prominent background with extra padding
        SDL_Rect bgRect = {textX - 20, textY - 10, textWidth + 40, textHeight + 20};
        SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), processingBgColor.r, processingBgColor.g, processingBgColor.b, processingBgColor.a);
        SDL_SetRenderDrawBlendMode(m_renderer->getSDLRenderer(), SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(m_renderer->getSDLRenderer(), &bgRect);
        
        // Draw bright border for maximum visibility
        SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), 255, 255, 0, 255);
        SDL_RenderDrawRect(m_renderer->getSDLRenderer(), &bgRect);
        
        // Draw text
        m_textRenderer->renderText(processingText, textX, textY, processingColor);
        
        // Restore font size
        m_textRenderer->setFontSize(100);
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
    
    // Render edge-turn progress indicator - only show when:
    // 1. D-pad or keyboard arrow is actively held and timer is active
    // 2. Content doesn't fit on screen in the movement direction (scrollable)
    // 3. There's a valid page to navigate to in the pressed direction
    bool inputHeld = m_dpadLeftHeld || m_dpadRightHeld || m_dpadUpHeld || m_dpadDownHeld ||
                     m_keyboardLeftHeld || m_keyboardRightHeld || m_keyboardUpHeld || m_keyboardDownHeld;
    float maxEdgeHold = std::max({m_edgeTurnHoldRight, m_edgeTurnHoldLeft, m_edgeTurnHoldUp, m_edgeTurnHoldDown});
    
    // Check scroll limits for each direction
    const int maxScrollX = m_viewportManager->getMaxScrollX();
    const int maxScrollY = m_viewportManager->getMaxScrollY();
    
    // Check if there are valid pages to navigate to in each direction
    bool canGoLeft = m_currentPage > 0;
    bool canGoRight = m_currentPage < m_pageCount - 1; 
    bool canGoUp = m_currentPage > 0;
    bool canGoDown = m_currentPage < m_pageCount - 1;
    
    // Only show progress bar when content doesn't fit in the movement direction
    // AND there's a valid page to navigate to
    bool validDirection = false;
    if ((m_dpadRightHeld || m_keyboardRightHeld) && m_edgeTurnHoldRight > 0.0f && canGoRight && maxScrollX > 0) {
        validDirection = true; // Show for horizontal movement when content doesn't fit horizontally
    }
    if ((m_dpadLeftHeld || m_keyboardLeftHeld) && m_edgeTurnHoldLeft > 0.0f && canGoLeft && maxScrollX > 0) {
        validDirection = true; // Show for horizontal movement when content doesn't fit horizontally
    }
    if ((m_dpadDownHeld || m_keyboardDownHeld) && m_edgeTurnHoldDown > 0.0f && canGoDown && maxScrollY > 0) {
        validDirection = true; // Show for vertical movement when content doesn't fit vertically
    }
    if ((m_dpadUpHeld || m_keyboardUpHeld) && m_edgeTurnHoldUp > 0.0f && canGoUp && maxScrollY > 0) {
        validDirection = true; // Show for vertical movement when content doesn't fit vertically
    }
    
    if (inputHeld && maxEdgeHold > 0.0f && validDirection) {
        float progress = maxEdgeHold / m_edgeTurnThreshold;
        if (progress > 0.05f) { // Only show indicator after 5% progress to avoid flicker
            // Enhance progress visualization for better completion feedback
            float visualProgress = std::min(progress * 1.1f, 1.0f); // Slightly faster visual progress
            
            // Determine which edge and direction
            std::string direction;
            int indicatorX = 0, indicatorY = 0;
            int barWidth = 200, barHeight = 20;
            
            if (m_edgeTurnHoldRight > 0.0f && (m_dpadRightHeld || m_keyboardRightHeld)) {
                direction = "Next Page";
                indicatorX = currentWindowWidth - barWidth - 20;
                indicatorY = currentWindowHeight / 2;
            } else if (m_edgeTurnHoldLeft > 0.0f && (m_dpadLeftHeld || m_keyboardLeftHeld)) {
                direction = "Previous Page";
                indicatorX = 20;
                indicatorY = currentWindowHeight / 2;
            } else if (m_edgeTurnHoldDown > 0.0f && (m_dpadDownHeld || m_keyboardDownHeld)) {
                direction = "Next Page";
                indicatorX = (currentWindowWidth - barWidth) / 2;
                indicatorY = currentWindowHeight - 60;
            } else if (m_edgeTurnHoldUp > 0.0f && (m_dpadUpHeld || m_keyboardUpHeld)) {
                direction = "Previous Page";
                indicatorX = (currentWindowWidth - barWidth) / 2;
                indicatorY = 40;
            }
            
            // Calculate text dimensions for better background sizing
            int avgCharWidth = 10; // Slightly wider character width estimation for better text spacing
            int textWidth = static_cast<int>(direction.length()) * avgCharWidth;
            int textHeight = 20; // Approximate height at 120% font size
            int textPadding = 12; // Increased padding around text for wider background
            
            // Position text above the progress bar
            int textX = indicatorX + (barWidth - textWidth) / 2;
            int textY = indicatorY - textHeight - textPadding - 5;
            
            // Draw text background container with semi-transparent background
            SDL_Rect textBgRect = {
                textX - textPadding, 
                textY - textPadding, 
                textWidth + 2 * textPadding, 
                textHeight + 2 * textPadding
            };
            SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), 0, 0, 0, 180); // Semi-transparent black
            SDL_SetRenderDrawBlendMode(m_renderer->getSDLRenderer(), SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(m_renderer->getSDLRenderer(), &textBgRect);
            
            // Draw text background border
            SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), 255, 255, 255, 255);
            SDL_RenderDrawRect(m_renderer->getSDLRenderer(), &textBgRect);
            
            // Draw progress bar background
            SDL_Rect bgRect = {indicatorX, indicatorY, barWidth, barHeight};
            SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), 50, 50, 50, 150);
            SDL_SetRenderDrawBlendMode(m_renderer->getSDLRenderer(), SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(m_renderer->getSDLRenderer(), &bgRect);
            
            // Draw progress bar
            int progressWidth = static_cast<int>(barWidth * visualProgress);
            if (progressWidth > 0) {
                SDL_Rect progressRect = {indicatorX, indicatorY, progressWidth, barHeight};
                
                // More dramatic color transition: bright yellow to bright green
                // Use a more aggressive curve to make the transition more visible
                float colorProgress = std::min(progress * 1.2f, 1.0f); // Accelerate color change
                uint8_t red = static_cast<uint8_t>(255 * (1.0f - colorProgress));
                uint8_t green = 255;
                uint8_t blue = static_cast<uint8_t>(colorProgress < 0.5f ? 0 : 100 * (colorProgress - 0.5f) * 2); // Add some blue at the end for more green
                
                SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), red, green, blue, 200);
                SDL_RenderFillRect(m_renderer->getSDLRenderer(), &progressRect);
            }
            
            // Draw progress bar border
            SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), 255, 255, 255, 255);
            SDL_RenderDrawRect(m_renderer->getSDLRenderer(), &bgRect);
            
            // Draw text label with white text
            SDL_Color labelColor = {255, 255, 255, 255};
            m_textRenderer->setFontSize(120); // 120% for visibility
            m_textRenderer->renderText(direction, textX, textY, labelColor);
            m_textRenderer->setFontSize(100); // Restore normal size
        }
    }
}

void App::goToNextPage()
{
    if (m_currentPage < m_pageCount - 1)
    {
        m_currentPage++;
        m_viewportManager->onPageChangedKeepZoom(m_document.get(), m_currentPage);
        m_viewportManager->alignToTopOfCurrentPage();
        updateScaleDisplayTime();
        updatePageDisplayTime();
        markDirty();
        
        // Cancel prerendering since we're changing pages
        auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
        if (muPdfDoc) {
            muPdfDoc->cancelPrerendering();
        }
        
        // Set cooldown timer to prevent rapid page changes during panning
        m_lastPageChangeTime = SDL_GetTicks();
        
        // Update GUI manager with current page
        if (m_guiManager) {
            m_guiManager->setCurrentPage(m_currentPage);
        }
    }
}

void App::goToPreviousPage()
{
    if (m_currentPage > 0)
    {
        m_currentPage--;
        m_viewportManager->onPageChangedKeepZoom(m_document.get(), m_currentPage);
        m_viewportManager->alignToTopOfCurrentPage();
        updateScaleDisplayTime();
        updatePageDisplayTime();
        markDirty();
        
        // Cancel prerendering since we're changing pages
        auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
        if (muPdfDoc) {
            muPdfDoc->cancelPrerendering();
        }
        
        // Set cooldown timer to prevent rapid page changes during panning
        m_lastPageChangeTime = SDL_GetTicks();
        
        // Update GUI manager with current page
        if (m_guiManager) {
            m_guiManager->setCurrentPage(m_currentPage);
        }
    }
}

void App::goToPage(int pageNum)
{
    if (pageNum >= 0 && pageNum < m_pageCount)
    {
        m_currentPage = pageNum;
        m_viewportManager->onPageChangedKeepZoom(m_document.get(), m_currentPage);
        m_viewportManager->alignToTopOfCurrentPage();
        updateScaleDisplayTime();
        updatePageDisplayTime();
        markDirty();
        
        // Update GUI manager with current page
        if (m_guiManager) {
            m_guiManager->setCurrentPage(m_currentPage);
        }
    }
}









void App::applyPendingFontChange()
{
    if (!m_pendingFontChange) {
        return; // No pending font change
    }
    
    std::cout << "DEBUG: Applying pending font change - " << m_pendingFontConfig.fontName 
              << " at " << m_pendingFontConfig.fontSize << "pt" << std::endl;
    
    // Generate CSS from the pending configuration
    if (m_optionsManager) {
        std::string css = m_optionsManager->generateCSS(m_pendingFontConfig);
        std::cout << "DEBUG: Generated CSS: " << css << std::endl;
        
        if (!css.empty()) {
            // Try to cast to MuPDF document and apply CSS with safer reopening
            if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get())) {
                // Store current state to restore after reopening
                int currentPage = m_currentPage;
                int currentScale = m_viewportManager->getCurrentScale();
                int currentScrollX = m_viewportManager->getScrollX();
                int currentScrollY = m_viewportManager->getScrollY();
                
                // Use the much safer reopening method
                if (muDoc->reopenWithCSS(css)) {
                    // Restore state after reopening with bounds checking
                    int pageCount = m_document->getPageCount();
                    if (currentPage >= 0 && currentPage < pageCount) {
                        m_currentPage = currentPage;
                    } else {
                        m_currentPage = 0; // Fallback to first page
                    }
                    
                    // Restore scale with reasonable bounds
                    if (currentScale >= 10 && currentScale <= 350) {
                        m_viewportManager->setCurrentScale(currentScale);
                    } else {
                        m_viewportManager->setCurrentScale(100); // Fallback to 100%
                    }
                    
                    // Restore scroll position (will be clamped later)
                    m_viewportManager->setScrollX(currentScrollX);
                    m_viewportManager->setScrollY(currentScrollY);
                    
                    // Update page count after reopening
                    m_pageCount = pageCount;
                    
                    // Clamp scroll to ensure it's within bounds
                    m_viewportManager->clampScroll();
                    
                    // Save the configuration
                    m_optionsManager->saveConfig(m_pendingFontConfig);
                    
                    // Force re-render of current page
                    markDirty();
                    
                    // Close the font menu after successful application
                    if (m_guiManager && m_guiManager->isFontMenuVisible()) {
                        m_guiManager->toggleFontMenu();
                    }
                    
                    std::cout << "Applied font configuration: " << m_pendingFontConfig.fontName 
                             << " at " << m_pendingFontConfig.fontSize << "pt" << std::endl;
                } else {
                    std::cout << "Failed to reopen document with new CSS" << std::endl;
                }
            } else {
                std::cout << "CSS styling not supported for this document type" << std::endl;
            }
        } else {
            std::cout << "Failed to generate CSS from font configuration" << std::endl;
        }
    } else {
        std::cout << "FontManager not available" << std::endl;
    }
    
    // Clear the pending flag
    m_pendingFontChange = false;
}









// ---- helpers  ----
void App::jumpPages(int delta)
{
    int target = std::clamp(m_currentPage + delta, 0, m_pageCount - 1);
    goToPage(target);
}









void App::printAppState()
{
    std::cout << "--- App State ---" << std::endl;
    std::cout << "Current Page: " << (m_currentPage + 1) << "/" << m_pageCount << std::endl;
    std::cout << "Native Page Dimensions: "
              << m_document->getPageWidthNative(m_currentPage) << "x"
              << m_document->getPageHeightNative(m_currentPage) << std::endl;
    std::cout << "Current Scale: " << m_viewportManager->getCurrentScale() << "%" << std::endl;
    std::cout << "Scaled Page Dimensions: " << m_viewportManager->getPageWidth() << "x" << m_viewportManager->getPageHeight() << " (Expected/Actual)" << std::endl;
    std::cout << "Scroll Position (Page Offset): X=" << m_viewportManager->getScrollX() << ", Y=" << m_viewportManager->getScrollY() << std::endl;
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

    if (m_dpadLeftHeld || m_keyboardLeftHeld) {
        dx += 1.0f;
    }
    if (m_dpadRightHeld || m_keyboardRightHeld) {
        dx -= 1.0f;
    }
    if (m_dpadUpHeld || m_keyboardUpHeld) {
        dy += 1.0f;
    }
    if (m_dpadDownHeld || m_keyboardDownHeld) {
        dy -= 1.0f;
    }

    // Check if we're in scroll timeout after a page change
    bool inScrollTimeout = isInScrollTimeout();
    
    // Track if scrolling actually happened this frame
    bool scrollingOccurred = false;
    
    // Enhanced stability: Force a brief pause after page changes to prevent warping
    // This gives the rendering system time to stabilize before processing new input
    bool inStabilizationPeriod = (SDL_GetTicks() - m_lastPageChangeTime) < (m_lastRenderDuration + 50);
    
    if (dx != 0.0f || dy != 0.0f)
    {
        if (inScrollTimeout || inStabilizationPeriod) {
            // During scroll timeout or stabilization period, don't allow panning movement
            // This prevents scrolling past the beginning of a new page and reduces warping
            // But we still need to continue processing edge-turn logic below
        } else {
            float len = std::sqrt(dx * dx + dy * dy);
            dx /= len;
            dy /= len;

            int oldScrollX = m_viewportManager->getScrollX();
            int oldScrollY = m_viewportManager->getScrollY();
            
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
            
            m_viewportManager->setScrollX(m_viewportManager->getScrollX() + pixelMoveX);
            m_viewportManager->setScrollY(m_viewportManager->getScrollY() + pixelMoveY);
            m_viewportManager->clampScroll();
            
            if (m_viewportManager->getScrollX() != oldScrollX || m_viewportManager->getScrollY() != oldScrollY) {
                changed = true;
                scrollingOccurred = true;
            }
        }
    }

    // --- HORIZONTAL edge → page turn ---
    const int maxX = m_viewportManager->getMaxScrollX();
    
    // Track old edge-turn values to detect changes for progress indicator updates
    float oldEdgeTurnHoldRight = m_edgeTurnHoldRight;
    float oldEdgeTurnHoldLeft = m_edgeTurnHoldLeft;
    float oldEdgeTurnHoldUp = m_edgeTurnHoldUp;
    float oldEdgeTurnHoldDown = m_edgeTurnHoldDown;

    // Reset edge-turn timers during scroll timeout to prevent accumulated time from previous page
    if (inScrollTimeout || inStabilizationPeriod) {
        // During stabilization period, gradually decay edge-turn timers instead of hard reset
        // This provides smoother visual feedback and reduces warping appearance
        if (inStabilizationPeriod && !inScrollTimeout) {
            // Gradual decay during stabilization period (but not timeout)
            float decayFactor = 0.95f; // Decay 5% per frame
            m_edgeTurnHoldRight *= decayFactor;
            m_edgeTurnHoldLeft *= decayFactor;
            m_edgeTurnHoldUp *= decayFactor;
            m_edgeTurnHoldDown *= decayFactor;
            
            // Reset to zero when very small to avoid floating point drift
            if (m_edgeTurnHoldRight < 0.01f) m_edgeTurnHoldRight = 0.0f;
            if (m_edgeTurnHoldLeft < 0.01f) m_edgeTurnHoldLeft = 0.0f;
            if (m_edgeTurnHoldUp < 0.01f) m_edgeTurnHoldUp = 0.0f;
            if (m_edgeTurnHoldDown < 0.01f) m_edgeTurnHoldDown = 0.0f;
        } else {
            // Hard reset during scroll timeout
            m_edgeTurnHoldRight = 0.0f;
            m_edgeTurnHoldLeft = 0.0f;
            m_edgeTurnHoldUp = 0.0f;
            m_edgeTurnHoldDown = 0.0f;
        }
    } else if (scrollingOccurred) {
        // Reset edge-turn timers if user is actively scrolling - only start timer when stationary at edge
        m_edgeTurnHoldRight = 0.0f;
        m_edgeTurnHoldLeft = 0.0f;
        m_edgeTurnHoldUp = 0.0f;
        m_edgeTurnHoldDown = 0.0f;
    } else {
        // Only accumulate edge-turn time when not in scroll timeout AND not actively scrolling
        if (maxX == 0)
        {
            if (m_dpadRightHeld || m_keyboardRightHeld) {
                m_edgeTurnHoldRight += dt;
            } else {
                m_edgeTurnHoldRight = 0.0f;
            }
            if (m_dpadLeftHeld || m_keyboardLeftHeld) {
                m_edgeTurnHoldLeft += dt;
            } else {
                m_edgeTurnHoldLeft = 0.0f;
            }
        }
        else
        {
            // Use small tolerance for edge detection to handle rounding issues
            const int edgeTolerance = 2; // pixels
            
            if (m_viewportManager->getScrollX() <= (-maxX + edgeTolerance) && (m_dpadRightHeld || m_keyboardRightHeld)) {
                float oldTime = m_edgeTurnHoldRight;
                m_edgeTurnHoldRight += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldRight > 0.0f) {
                    printf("DEBUG: Right edge-turn timer started (scroll-based, scrollX=%d, threshold=%d)\n", m_viewportManager->getScrollX(), -maxX + edgeTolerance);
                }
            } else {
                if ((m_dpadRightHeld || m_keyboardRightHeld) && m_edgeTurnHoldRight > 0.0f) {
                    printf("DEBUG: Right edge-turn timer stopped (scrollX=%d, threshold=%d, held=%s)\n", 
                           m_viewportManager->getScrollX(), -maxX + edgeTolerance, (m_dpadRightHeld || m_keyboardRightHeld) ? "YES" : "NO");
                }
                m_edgeTurnHoldRight = 0.0f;
            }
            if (m_viewportManager->getScrollX() >= (maxX - edgeTolerance) && (m_dpadLeftHeld || m_keyboardLeftHeld)) {
                float oldTime = m_edgeTurnHoldLeft;
                m_edgeTurnHoldLeft += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldLeft > 0.0f) {
                    printf("DEBUG: Left edge-turn timer started (scroll-based, scrollX=%d, threshold=%d)\n", m_viewportManager->getScrollX(), maxX - edgeTolerance);
                }
            } else {
                if ((m_dpadLeftHeld || m_keyboardLeftHeld) && m_edgeTurnHoldLeft > 0.0f) {
                    printf("DEBUG: Left edge-turn timer stopped (scrollX=%d, threshold=%d, held=%s)\n", 
                           m_viewportManager->getScrollX(), maxX - edgeTolerance, (m_dpadLeftHeld || m_keyboardLeftHeld) ? "YES" : "NO");
                }
                m_edgeTurnHoldLeft = 0.0f;
            }
        }
    }

    if (m_edgeTurnHoldRight >= m_edgeTurnThreshold)
    {
        // Check cooldown before allowing page change
        float currentTime = SDL_GetTicks() / 1000.0f;
        bool inCooldown = (m_edgeTurnCooldownRight > 0.0f) && 
                         (currentTime - m_edgeTurnCooldownRight < m_edgeTurnCooldownDuration);
        
        if (!inCooldown && m_currentPage < m_pageCount - 1 && !isInPageChangeCooldown())
        {
            printf("DEBUG: Right edge-turn completed - page change allowed\n");
            goToNextPage();
            m_viewportManager->setScrollX(m_viewportManager->getMaxScrollX()); // appear at left edge
            m_viewportManager->clampScroll();
            changed = true;
        }
        else if (inCooldown)
        {
            printf("DEBUG: Right edge-turn completed - blocked by cooldown (%.3fs remaining)\n", 
                   m_edgeTurnCooldownDuration - (currentTime - m_edgeTurnCooldownRight));
        }
        m_edgeTurnHoldRight = 0.0f;
    }
    else if (m_edgeTurnHoldLeft >= m_edgeTurnThreshold)
    {
        // Check cooldown before allowing page change
        float currentTime = SDL_GetTicks() / 1000.0f;
        bool inCooldown = (m_edgeTurnCooldownLeft > 0.0f) && 
                         (currentTime - m_edgeTurnCooldownLeft < m_edgeTurnCooldownDuration);
        
        if (!inCooldown && m_currentPage > 0 && !isInPageChangeCooldown())
        {
            printf("DEBUG: Left edge-turn completed - page change allowed\n");
            goToPreviousPage();
            m_viewportManager->setScrollX(-m_viewportManager->getMaxScrollX()); // appear at right edge
            m_viewportManager->clampScroll();
            changed = true;
        }
        else if (inCooldown)
        {
            printf("DEBUG: Left edge-turn completed - blocked by cooldown (%.3fs remaining)\n", 
                   m_edgeTurnCooldownDuration - (currentTime - m_edgeTurnCooldownLeft));
        }
        m_edgeTurnHoldLeft = 0.0f;
    }

    // --- VERTICAL edge → page turn (NEW) ---
    const int maxY = m_viewportManager->getMaxScrollY();

    if (!inScrollTimeout && !scrollingOccurred) {
        // Only accumulate edge-turn time when not in scroll timeout AND not actively scrolling
        if (maxY == 0)
        {
            // Page fits vertically: treat sustained up/down as page turns
            if (m_dpadDownHeld || m_keyboardDownHeld) {
                float oldTime = m_edgeTurnHoldDown;
                m_edgeTurnHoldDown += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldDown > 0.0f) {
                    printf("DEBUG: Down edge-turn timer started (maxY=0)\n");
                }
            } else {
                m_edgeTurnHoldDown = 0.0f;
            }
            if (m_dpadUpHeld || m_keyboardUpHeld) {
                float oldTime = m_edgeTurnHoldUp;
                m_edgeTurnHoldUp += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldUp > 0.0f) {
                    printf("DEBUG: Up edge-turn timer started (maxY=0)\n");
                }
            } else {
                m_edgeTurnHoldUp = 0.0f;
            }
        }
        else
        {
            // Use small tolerance for edge detection to handle rounding issues
            const int edgeTolerance = 2; // pixels
            
            // Bottom edge & still pushing down? (down moves view further down in your scheme: dy < 0)
            if (m_viewportManager->getScrollY() <= (-maxY + edgeTolerance) && (m_dpadDownHeld || m_keyboardDownHeld)) {
                float oldTime = m_edgeTurnHoldDown;
                m_edgeTurnHoldDown += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldDown > 0.0f) {
                    printf("DEBUG: Down edge-turn timer started (scroll-based)\n");
                }
            } else {
                m_edgeTurnHoldDown = 0.0f;
            }

            // Top edge & still pushing up?
            if (m_viewportManager->getScrollY() >= (maxY - edgeTolerance) && (m_dpadUpHeld || m_keyboardUpHeld)) {
                float oldTime = m_edgeTurnHoldUp;
                m_edgeTurnHoldUp += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldUp > 0.0f) {
                    printf("DEBUG: Up edge-turn timer started (scroll-based)\n");
                }
            } else {
                m_edgeTurnHoldUp = 0.0f;
            }
        }
    } else if (scrollingOccurred) {
        // Reset vertical edge-turn timers if actively scrolling
        if (m_edgeTurnHoldUp > 0.0f || m_edgeTurnHoldDown > 0.0f) {
            printf("DEBUG: Resetting vertical edge-turn timers due to active scrolling\n");
        }
        m_edgeTurnHoldUp = 0.0f;
        m_edgeTurnHoldDown = 0.0f;
    }

    if (m_edgeTurnHoldDown >= m_edgeTurnThreshold)
    {
        // Check cooldown before allowing page change
        float currentTime = SDL_GetTicks() / 1000.0f;
        bool inCooldown = (m_edgeTurnCooldownDown > 0.0f) && 
                         (currentTime - m_edgeTurnCooldownDown < m_edgeTurnCooldownDuration);
        
        if (!inCooldown && m_currentPage < m_pageCount - 1 && !isInPageChangeCooldown())
        {
            printf("DEBUG: Down edge-turn completed - page change allowed\n");
            goToNextPage();
            // Land at the top edge of the new page so motion feels continuous downward
            m_viewportManager->setScrollY(m_viewportManager->getMaxScrollY());
            m_viewportManager->clampScroll();
            changed = true;
        }
        else if (inCooldown)
        {
            printf("DEBUG: Down edge-turn completed - blocked by cooldown (%.3fs remaining)\n", 
                   m_edgeTurnCooldownDuration - (currentTime - m_edgeTurnCooldownDown));
        }
        m_edgeTurnHoldDown = 0.0f;
    }
    else if (m_edgeTurnHoldUp >= m_edgeTurnThreshold)
    {
        // Check cooldown before allowing page change
        float currentTime = SDL_GetTicks() / 1000.0f;
        bool inCooldown = (m_edgeTurnCooldownUp > 0.0f) && 
                         (currentTime - m_edgeTurnCooldownUp < m_edgeTurnCooldownDuration);
        
        if (!inCooldown && m_currentPage > 0 && !isInPageChangeCooldown())
        {
            printf("DEBUG: Up edge-turn completed - page change allowed\n");
            goToPreviousPage();
            // Land at the bottom edge of the previous page
            m_viewportManager->setScrollY(-m_viewportManager->getMaxScrollY());
            m_viewportManager->clampScroll();
            changed = true;
        }
        else if (inCooldown)
        {
            printf("DEBUG: Up edge-turn completed - blocked by cooldown (%.3fs remaining)\n", 
                   m_edgeTurnCooldownDuration - (currentTime - m_edgeTurnCooldownUp));
        }
        m_edgeTurnHoldUp = 0.0f;
    }
    
    // Check if any edge-turn timing values changed and mark as dirty for progress indicator updates
    if (m_edgeTurnHoldRight != oldEdgeTurnHoldRight ||
        m_edgeTurnHoldLeft != oldEdgeTurnHoldLeft ||
        m_edgeTurnHoldUp != oldEdgeTurnHoldUp ||
        m_edgeTurnHoldDown != oldEdgeTurnHoldDown) {
        markDirty();
    }
    
    return changed;
}



void App::handleDpadNudgeRight()
{
    const int maxX = m_viewportManager->getMaxScrollX();
    
    printf("DEBUG: Right nudge called - maxX=%d, scrollX=%d, condition=%s, inScrollTimeout=%s\n", 
           maxX, m_viewportManager->getScrollX(), (maxX == 0 || m_viewportManager->getScrollX() <= (-maxX + 2)) ? "AT_EDGE" : "NOT_AT_EDGE",
           isInScrollTimeout() ? "YES" : "NO");
    
    // Right nudge while already at right edge
    if (maxX == 0 || m_viewportManager->getScrollX() <= (-maxX + 2)) // Use same tolerance as edge-turn system
    {
        if (maxX == 0) {
            // Page fits horizontally (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldRight == 0.0f) // Only if no progress bar is currently running
            {
                if (m_currentPage < m_pageCount - 1 && !isInPageChangeCooldown())
                {
                    printf("DEBUG: Immediate page change via nudge (fit-to-width)\n");
                    goToNextPage();
                    m_viewportManager->setScrollX(m_viewportManager->getMaxScrollX()); // appear at left edge of new page
                    m_viewportManager->clampScroll();
                }
            }
        } else {
            // For zoomed pages (maxX > 0): defer to progress bar system
            printf("DEBUG: Zoomed page at edge - deferring to progress bar system\n");
        }
        return;
    }
    printf("DEBUG: Normal scroll - moving right\n");
    m_viewportManager->setScrollX(m_viewportManager->getScrollX() - 50);
    m_viewportManager->clampScroll();
}

void App::handleDpadNudgeLeft()
{
    const int maxX = m_viewportManager->getMaxScrollX();
    
    printf("DEBUG: Left nudge called - maxX=%d, scrollX=%d, condition=%s\n", 
           maxX, m_viewportManager->getScrollX(), (maxX == 0 || m_viewportManager->getScrollX() >= (maxX - 2)) ? "AT_EDGE" : "NOT_AT_EDGE");
    
    // Left nudge while already at left edge
    if (maxX == 0 || m_viewportManager->getScrollX() >= (maxX - 2)) // Use same tolerance as edge-turn system
    {
        if (maxX == 0) {
            // Page fits horizontally (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldLeft == 0.0f) // Only if no progress bar is currently running
            {
                if (m_currentPage > 0 && !isInPageChangeCooldown())
                {
                    goToPreviousPage();
                    m_viewportManager->setScrollX(-m_viewportManager->getMaxScrollX()); // appear at right edge of prev page
                    m_viewportManager->clampScroll();
                }
            }
        }
        // For zoomed pages (maxX > 0): always defer to progress bar system
        // This ensures a progress bar always appears when holding D-pad at edge
        return;
    }
    m_viewportManager->setScrollX(m_viewportManager->getScrollX() + 50);
    m_viewportManager->clampScroll();
}

void App::handleDpadNudgeDown()
{
    const int maxY = m_viewportManager->getMaxScrollY();
    
    printf("DEBUG: Down nudge called - maxY=%d, scrollY=%d, condition=%s\n", 
           maxY, m_viewportManager->getScrollY(), (maxY == 0 || m_viewportManager->getScrollY() <= (-maxY + 2)) ? "AT_EDGE" : "NOT_AT_EDGE");
    
    // Down nudge while already at bottom edge
    if (maxY == 0 || m_viewportManager->getScrollY() <= (-maxY + 2)) // Use same tolerance as edge-turn system
    {
        if (maxY == 0) {
            // Page fits vertically (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldDown == 0.0f) // Only if no progress bar is currently running
            {
                if (m_currentPage < m_pageCount - 1 && !isInPageChangeCooldown())
                {
                    goToNextPage();
                    m_viewportManager->setScrollY(m_viewportManager->getMaxScrollY()); // appear at top edge of new page
                    m_viewportManager->clampScroll();
                }
            }
        }
        // For zoomed pages (maxY > 0): always defer to progress bar system
        // This ensures a progress bar always appears when holding D-pad at edge
        return;
    }
    m_viewportManager->setScrollY(m_viewportManager->getScrollY() - 50);
    m_viewportManager->clampScroll();
}

void App::handleDpadNudgeUp()
{
    const int maxY = m_viewportManager->getMaxScrollY();
    
    printf("DEBUG: Up nudge called - maxY=%d, scrollY=%d, condition=%s\n", 
           maxY, m_viewportManager->getScrollY(), (maxY == 0 || m_viewportManager->getScrollY() >= (maxY - 2)) ? "AT_EDGE" : "NOT_AT_EDGE");
    
    // Up nudge while already at top edge
    if (maxY == 0 || m_viewportManager->getScrollY() >= (maxY - 2)) // Use same tolerance as edge-turn system
    {
        if (maxY == 0) {
            // Page fits vertically (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldUp == 0.0f) // Only if no progress bar is currently running
            {
                if (m_currentPage > 0 && !isInPageChangeCooldown())
                {
                    goToPreviousPage();
                    m_viewportManager->setScrollY(-m_viewportManager->getMaxScrollY()); // appear at bottom edge of prev page
                    m_viewportManager->clampScroll();
                }
            }
        }
        // For zoomed pages (maxY > 0): always defer to progress bar system
        // This ensures a progress bar always appears when holding D-pad at edge
        return;
    }
    m_viewportManager->setScrollY(m_viewportManager->getScrollY() + 50);
    m_viewportManager->clampScroll();
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

void App::toggleFontMenu()
{
    if (m_guiManager) {
        m_guiManager->toggleFontMenu();
        markDirty(); // Force redraw to show/hide the menu
    }
}

void App::applyFontConfiguration(const FontConfig& config)
{
    if (!m_document) {
        std::cerr << "Cannot apply font configuration: no document loaded" << std::endl;
        return;
    }
    
    std::cout << "DEBUG: Scheduling deferred font change - " << config.fontName 
              << " at " << config.fontSize << "pt" << std::endl;
    
    // Store the configuration for deferred processing in the main loop
    m_pendingFontConfig = config;
    m_pendingFontChange = true;
    
    // The actual document reopening will happen safely in the main loop
    // via applyPendingFontChange()
}
