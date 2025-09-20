#include "app.h"
#include "renderer.h"
#include "text_renderer.h"
#include "document.h"
#include "mupdf_document.h"
#include "gui_manager.h"
#include "font_manager.h"
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
            markDirty(); // Force screen redraw to show black screen
        } else {
            std::cout << "App: Exiting fake sleep mode - re-enabling inputs and screen" << std::endl;
            markDirty(); // Force screen redraw to restore normal display
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
          lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".cbr" ||
          lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".rar" ||
          lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".zip")) ||
        (lowercaseFilename.size() >= 5 && 
         (lowercaseFilename.substr(lowercaseFilename.size() - 5) == ".epub" ||
          lowercaseFilename.substr(lowercaseFilename.size() - 5) == ".mobi"))) {
        m_document = std::make_unique<MuPdfDocument>();
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

    // Initialize game controllers
    initializeGameControllers();

    // Initialize font manager first
    m_fontManager = std::make_unique<FontManager>();

    // Load saved font configuration (but don't apply it yet)
    FontConfig savedConfig = m_fontManager->loadConfig();
    
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

    // TODO: Temporarily disable auto-loading saved config to test if that's causing the crash
    /*
    // Now it's safe to apply saved configuration and update GUI
    if (!savedConfig.fontPath.empty()) {
        applyFontConfiguration(savedConfig);
        m_guiManager->setCurrentFontConfig(savedConfig);
    }
    */
}

App::~App()
{
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
        // Track if we started an ImGui frame this iteration
        bool imguiFrameStarted = false;
        
        // Check if we need to render ImGui this frame (before processing events)
        bool willRenderImGui = m_guiManager && !m_inFakeSleep && 
                              (m_guiManager->isFontMenuVisible() || m_needsRedraw);
        
        // Start ImGui frame BEFORE event processing if we'll render ImGui
        if (willRenderImGui) {
            m_guiManager->newFrame();
            imguiFrameStarted = true;
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
            
            // Check for settled zoom input and apply pending zoom
            if (m_pendingZoomDelta != 0) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastZoomInputTime).count();
                
                // Use platform-specific debounce time, with minimum based on render performance
#ifdef TG5040_PLATFORM
                int debounceTime = std::max(ZOOM_DEBOUNCE_MS, static_cast<int>(m_lastRenderDuration));
#else
                int debounceTime = std::max(ZOOM_DEBOUNCE_MS, static_cast<int>(m_lastRenderDuration));
#endif
                
                if (elapsed >= debounceTime) {
                    // Zoom input has settled, apply the final accumulated zoom
                    applyPendingZoom();
                }
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
                shouldRender = (m_needsRedraw || panningChanged) && 
                              (currentTime - lastRenderTime) >= 16; // Max 60 FPS to prevent overload
            }
            
            bool doRender = false;
            
            if (isZoomDebouncing()) {
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
                // Start ImGui frame if we haven't already
                if (m_guiManager && !imguiFrameStarted) {
                    m_guiManager->newFrame();
                }
                
                renderCurrentPage();
                renderUI();
                
                // Render ImGui
                if (m_guiManager) {
                    m_guiManager->render();
                }
                
                m_renderer->present();
                lastRenderTime = currentTime;
                
                // Only reset needsRedraw for normal rendering, not during zoom debouncing
                if (!isZoomDebouncing()) {
                    m_needsRedraw = false;
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
    // Debug only ESC key events
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        std::cout << "ESC key pressed" << std::endl;
    }
    
    // Let ImGui handle the event first
    if (m_guiManager && m_guiManager->handleEvent(event)) {
        // If ImGui handled the event, don't process it further for non-keyboard events
        if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) {
            return;
        }
        
        // For keyboard events, check if ImGui wants keyboard capture only if menu is visible
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            // If font menu is visible, let ImGui handle all keyboard input except specific cases
            if (m_guiManager->isFontMenuVisible()) {
                // ESC key should close the menu, so don't let ImGui consume it
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    // Let ESC through to close menu
                } else if (m_guiManager->wantsCaptureKeyboard()) {
                    return;
                }
            }
        }
    }

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
            if (m_pageJumpInputActive) {
                cancelPageJumpInput();
            } else if (m_guiManager && m_guiManager->isFontMenuVisible()) {
                // Close font menu if it's open
                m_guiManager->toggleFontMenu();
                // Force redraw to clear the menu from screen
                markDirty();
                // Don't allow quit action when closing menu
            } else {
                action = AppAction::Quit;
            }
            break;
        case SDLK_q:
            if (m_pageJumpInputActive) {
                cancelPageJumpInput();
            } else if (m_guiManager && m_guiManager->isFontMenuVisible()) {
                // Close font menu if it's open - q key can also close menu
                m_guiManager->toggleFontMenu();
                // Force redraw to clear the menu from screen  
                markDirty();
            } else {
                action = AppAction::Quit;
            }
            break;
        case SDLK_RIGHT:
            m_keyboardRightHeld = true;
            if (!isInScrollTimeout()) {
                handleDpadNudgeRight();
                updatePageDisplayTime();
                markDirty();
            }
            break;
        case SDLK_LEFT:
            m_keyboardLeftHeld = true;
            if (!isInScrollTimeout()) {
                handleDpadNudgeLeft();
                updatePageDisplayTime();
                markDirty();
            }
            break;
        case SDLK_UP:
            m_keyboardUpHeld = true;
            if (!isInScrollTimeout()) {
                handleDpadNudgeUp();
                updatePageDisplayTime();
                markDirty();
            }
            break;
        case SDLK_DOWN:
            m_keyboardDownHeld = true;
            if (!isInScrollTimeout()) {
                handleDpadNudgeDown();
                updatePageDisplayTime();
                markDirty();
            }
            break;
        case SDLK_PAGEDOWN:
            if (!isInPageChangeCooldown()) {
                std::cout << "DEBUG: PAGEDOWN key pressed - calling goToNextPage" << std::endl;
                goToNextPage();
            } else {
                std::cout << "DEBUG: PAGEDOWN blocked by cooldown" << std::endl;
            }
            break;
        case SDLK_PAGEUP:
            if (!isInPageChangeCooldown()) {
                std::cout << "DEBUG: PAGEUP key pressed - calling goToPreviousPage" << std::endl;
                goToPreviousPage();
            } else {
                std::cout << "DEBUG: PAGEUP blocked by cooldown" << std::endl;
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
        case SDLK_m:
            std::cout << "DEBUG: M key pressed - triggering ToggleFontMenu" << std::endl;
            action = AppAction::ToggleFontMenu;
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
        default:
            // Unknown keys are ignored
            break;
        }
        break;
    case SDL_KEYUP:
        switch (event.key.keysym.sym)
        {
        case SDLK_RIGHT:
            m_keyboardRightHeld = false;
            if (m_edgeTurnHoldRight > 0.0f) { // Only set cooldown if timer was actually running
                m_edgeTurnCooldownRight = SDL_GetTicks() / 1000.0f;
                printf("DEBUG: Keyboard Right edge-turn cancelled, timer was %.3f, cooldown set to %.3f\n", 
                       m_edgeTurnHoldRight, m_edgeTurnCooldownRight);
            }
            m_edgeTurnHoldRight = 0.0f; // Reset edge timer when key released
            markDirty(); // Trigger redraw to hide progress indicator
            break;
        case SDLK_LEFT:
            m_keyboardLeftHeld = false;
            if (m_edgeTurnHoldLeft > 0.0f) { // Only set cooldown if timer was actually running
                m_edgeTurnCooldownLeft = SDL_GetTicks() / 1000.0f;
                printf("DEBUG: Keyboard Left edge-turn cancelled, timer was %.3f, cooldown set to %.3f\n", 
                       m_edgeTurnHoldLeft, m_edgeTurnCooldownLeft);
            }
            m_edgeTurnHoldLeft = 0.0f; // Reset edge timer when key released
            markDirty(); // Trigger redraw to hide progress indicator
            break;
        case SDLK_UP:
            m_keyboardUpHeld = false;
            if (m_edgeTurnHoldUp > 0.0f) { // Only set cooldown if timer was actually running
                m_edgeTurnCooldownUp = SDL_GetTicks() / 1000.0f;
                printf("DEBUG: Keyboard Up edge-turn cancelled, timer was %.3f, cooldown set to %.3f\n", 
                       m_edgeTurnHoldUp, m_edgeTurnCooldownUp);
            }
            m_edgeTurnHoldUp = 0.0f; // Reset edge timer when key released
            markDirty(); // Trigger redraw to hide progress indicator
            break;
        case SDLK_DOWN:
            m_keyboardDownHeld = false;
            if (m_edgeTurnHoldDown > 0.0f) { // Only set cooldown if timer was actually running
                m_edgeTurnCooldownDown = SDL_GetTicks() / 1000.0f;
                printf("DEBUG: Keyboard Down edge-turn cancelled, timer was %.3f, cooldown set to %.3f\n", 
                       m_edgeTurnHoldDown, m_edgeTurnCooldownDown);
            }
            m_edgeTurnHoldDown = 0.0f; // Reset edge timer when key released
            markDirty(); // Trigger redraw to hide progress indicator
            break;
        default:
            // Unknown key releases are ignored
            break;
        }
        break;
    case SDL_MOUSEWHEEL:
        if (event.wheel.y > 0)
        {
            if (SDL_GetModState() & KMOD_CTRL)
            {
                zoom(10);
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
                zoom(-10);
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
                zoom(10);
                break;
            case SDL_CONTROLLER_BUTTON_B:
                zoom(-10);
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
                if (m_edgeTurnHoldRight > 0.0f) { // Only set cooldown if timer was actually running
                    m_edgeTurnCooldownRight = SDL_GetTicks() / 1000.0f;
                    printf("DEBUG: Right edge-turn cancelled, timer was %.3f, cooldown set to %.3f\n", 
                           m_edgeTurnHoldRight, m_edgeTurnCooldownRight);
                }
                m_edgeTurnHoldRight = 0.0f; // Reset edge timer when button released
                markDirty(); // Trigger redraw to hide progress indicator
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                m_dpadLeftHeld = false;
                if (m_edgeTurnHoldLeft > 0.0f) { // Only set cooldown if timer was actually running
                    m_edgeTurnCooldownLeft = SDL_GetTicks() / 1000.0f;
                    printf("DEBUG: Left edge-turn cancelled, timer was %.3f, cooldown set to %.3f\n", 
                           m_edgeTurnHoldLeft, m_edgeTurnCooldownLeft);
                }
                m_edgeTurnHoldLeft = 0.0f; // Reset edge timer when button released
                markDirty(); // Trigger redraw to hide progress indicator
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                m_dpadUpHeld = false;
                if (m_edgeTurnHoldUp > 0.0f) { // Only set cooldown if timer was actually running
                    m_edgeTurnCooldownUp = SDL_GetTicks() / 1000.0f;
                    printf("DEBUG: Up edge-turn cancelled, timer was %.3f, cooldown set to %.3f\n", 
                           m_edgeTurnHoldUp, m_edgeTurnCooldownUp);
                }
                m_edgeTurnHoldUp = 0.0f; // Reset edge timer when button released
                markDirty(); // Trigger redraw to hide progress indicator
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                m_dpadDownHeld = false;
                if (m_edgeTurnHoldDown > 0.0f) { // Only set cooldown if timer was actually running
                    m_edgeTurnCooldownDown = SDL_GetTicks() / 1000.0f;
                    printf("DEBUG: Down edge-turn cancelled, timer was %.3f, cooldown set to %.3f\n", 
                           m_edgeTurnHoldDown, m_edgeTurnCooldownDown);
                }
                m_edgeTurnHoldDown = 0.0f; // Reset edge timer when button released
                markDirty(); // Trigger redraw to hide progress indicator
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
    case SDL_JOYBUTTONDOWN:
        // Handle joystick button presses
        {
            // Handle specific button functions
            switch (event.jbutton.button) {
            case 9:
                // Button 9 - Reset page view (like R key)
                resetPageView();
                break;
            case 10:
                // Button 10 - Set zoom to 200%
                zoomTo(200);
                break;
            default:
                // Other joystick buttons are ignored
                break;
            }
        }
        break;
    case SDL_JOYBUTTONUP:
        // Joystick button releases are ignored
        break;
    case SDL_JOYHATMOTION:
        // Joystick hat motion is ignored
        break;
    case SDL_JOYAXISMOTION:
        // Joystick axis motion is ignored
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
    else if (action == AppAction::ToggleFontMenu)
    {
        std::cout << "DEBUG: Executing ToggleFontMenu action" << std::endl;
        toggleFontMenu();
    }
}

void App::loadDocument()
{
    m_currentPage = 0;
    fitPageToWindow();
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
                argbData = muPdfDoc->renderPageARGB(m_currentPage, srcW, srcH, m_currentScale);
            } catch (const std::exception& e) {
                // Fallback to RGB rendering
                std::vector<uint8_t> rgbData = m_document->renderPage(m_currentPage, srcW, srcH, m_currentScale);
                // Convert to ARGB manually
                argbData.resize(srcW * srcH);
                for (int i = 0; i < srcW * srcH; ++i) {
                    argbData[i] = rgb24_to_argb32(rgbData[i*3], rgbData[i*3+1], rgbData[i*3+2]);
                }
            }
        } else {
            // Fallback to RGB rendering for other document types
            std::vector<uint8_t> rgbData = m_document->renderPage(m_currentPage, srcW, srcH, m_currentScale);
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
    if (m_rotation % 180 == 0)
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
    if (m_pageWidth != newPageWidth || m_pageHeight != newPageHeight) {
        m_pageWidth = newPageWidth;
        m_pageHeight = newPageHeight;
        // Clamp scroll position when page dimensions change to prevent out-of-bounds rendering
        clampScroll();
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

    m_renderer->renderPageExARGB(argbData, srcW, srcH,
                                 posX, posY, m_pageWidth, m_pageHeight,
                                 static_cast<double>(m_rotation),
                                 currentFlipFlags());
    
    // Trigger prerendering of adjacent pages for faster page changes
    // Do this after the main render to avoid blocking the current page display
    // Only prerender if zoom is stable (not debouncing), not actively panning, and not in cooldown
    static Uint32 lastPrerenderTrigger = 0;
    Uint32 currentTime = SDL_GetTicks();
    bool prerenderCooldownActive = (currentTime - lastPrerenderTrigger) < 200; // 200ms cooldown
    
    if (!isZoomDebouncing() && !m_isDragging && !prerenderCooldownActive) {
        auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
        if (muPdfDoc && !muPdfDoc->isPrerenderingActive()) {
            muPdfDoc->prerenderAdjacentPagesAsync(m_currentPage, m_currentScale);
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
    
    // Render zoom processing indicator
    if (shouldShowZoomProcessingIndicator()) {
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
    const int maxScrollX = getMaxScrollX();
    const int maxScrollY = getMaxScrollY();
    
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
    std::cout << "DEBUG: goToNextPage called - current page " << m_currentPage << std::endl;
    if (m_currentPage < m_pageCount - 1)
    {
        m_currentPage++;
        std::cout << "DEBUG: Moving to page " << m_currentPage << std::endl;
        onPageChangedKeepZoom();
        alignToTopOfCurrentPage();
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
        
        // Cancel prerendering since we're changing pages
        auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
        if (muPdfDoc) {
            muPdfDoc->cancelPrerendering();
        }
        
        // Set cooldown timer to prevent rapid page changes during panning
        m_lastPageChangeTime = SDL_GetTicks();
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
    }
}

void App::zoom(int delta)
{
    // Accumulate zoom changes and track input timing for debouncing
    auto now = std::chrono::steady_clock::now();
    
    // Cancel any ongoing prerendering since zoom is changing
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
    if (muPdfDoc) {
        muPdfDoc->cancelPrerendering();
    }
    
    // Add to pending zoom delta
    m_pendingZoomDelta += delta;
    m_lastZoomInputTime = now;
    
    // Set zoom processing indicator and show it immediately for expensive operations
    if (!m_zoomProcessing) {
        m_zoomProcessing = true;
        m_zoomProcessingStartTime = now;
        
        // Show immediate processing indicator if recent renders have been expensive
        // Only render UI overlay, not the expensive page content
        if (isNextRenderLikelyExpensive()) {
            // Clear screen and show processing message without expensive page render
            m_renderer->clear(0, 0, 0, 255); // Black background
            renderUI(); // Only render the UI overlay with processing message
            m_renderer->present();
        }
    }
    
    // If there's already a pending zoom, don't apply immediately
    // The render loop will check for settled input and apply the final zoom
}

void App::zoomTo(int scale)
{
    // Always use debouncing for consistent behavior
    int targetDelta = scale - m_currentScale;
    
    // Cancel any ongoing prerendering since zoom is changing
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
    if (muPdfDoc) {
        muPdfDoc->cancelPrerendering();
    }
    
    // Set pending zoom delta to target and use debouncing
    m_pendingZoomDelta = targetDelta;
    m_lastZoomInputTime = std::chrono::steady_clock::now();
    
    // Set zoom processing indicator and show it immediately for expensive operations
    if (!m_zoomProcessing) {
        m_zoomProcessing = true;
        m_zoomProcessingStartTime = m_lastZoomInputTime;
        
        // Show immediate processing indicator if recent renders have been expensive
        // Only render UI overlay, not the expensive page content
        if (isNextRenderLikelyExpensive()) {
            // Clear screen and show processing message without expensive page render
            m_renderer->clear(0, 0, 0, 255); // Black background
            renderUI(); // Only render the UI overlay with processing message
            m_renderer->present();
        }
    }
}

void App::applyPendingZoom()
{
    if (m_pendingZoomDelta == 0) {
        return; // No pending zoom to apply
    }

    int oldScale = m_currentScale;
    m_currentScale += m_pendingZoomDelta;
    if (m_currentScale < 10)
        m_currentScale = 10;
    if (m_currentScale > 350)
        m_currentScale = 350;

    recenterScrollOnZoom(oldScale, m_currentScale);
    clampScroll();
    updateScaleDisplayTime();
    updatePageDisplayTime();
    markDirty();
    
    // Reset pending zoom and clear processing indicator (respecting minimum display time)
    m_pendingZoomDelta = 0;
    
    // Only clear zoom processing if minimum display time has elapsed
    if (m_zoomProcessing) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_zoomProcessingStartTime).count();
        if (elapsed >= ZOOM_PROCESSING_MIN_DISPLAY_MS) {
            m_zoomProcessing = false;
        }
    }
}

bool App::isZoomDebouncing() const
{
    return m_pendingZoomDelta != 0;
}

void App::fitPageToWindow()
{
    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

#ifndef TG5040_PLATFORM
// Update max render size for downsampling - allow for meaningful zoom levels on non-TG5040 platforms
// Use 4x window size to enable proper zooming while TG5040 has no limit
if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get()))
{
    // Allow 4x zoom by setting max render size to 4x window size
    muDoc->setMaxRenderSize(windowWidth * 4, windowHeight * 4);
}
#endif    // Use effective sizes so 90/270 rotation swaps W/H
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

    // Update page dimensions based on effective size (accounting for downsampling)
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
    if (muPdfDoc) {
        m_pageWidth = muPdfDoc->getPageWidthEffective(m_currentPage, m_currentScale);
        m_pageHeight = muPdfDoc->getPageHeightEffective(m_currentPage, m_currentScale);
        
        // Apply rotation
        if (m_rotation % 180 != 0) {
            std::swap(m_pageWidth, m_pageHeight);
        }
    } else {
        // Fallback for other document types
        m_pageWidth = static_cast<int>(nativeWidth * (m_currentScale / 100.0));
        m_pageHeight = static_cast<int>(nativeHeight * (m_currentScale / 100.0));
    }

    m_scrollX = 0;
    m_scrollY = 0;
    updateScaleDisplayTime();
    updatePageDisplayTime();
    markDirty();
    
}

void App::recenterScrollOnZoom(int oldScale, int newScale)
{
    if (oldScale == 0 || newScale == 0)
        return;

    // Use effective dimensions that account for downsampling
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
    int oldPageWidth, oldPageHeight, newPageWidth, newPageHeight;
    
    if (muPdfDoc) {
        oldPageWidth = muPdfDoc->getPageWidthEffective(m_currentPage, oldScale);
        oldPageHeight = muPdfDoc->getPageHeightEffective(m_currentPage, oldScale);
        newPageWidth = muPdfDoc->getPageWidthEffective(m_currentPage, newScale);
        newPageHeight = muPdfDoc->getPageHeightEffective(m_currentPage, newScale);
        
        // Apply rotation
        if (m_rotation % 180 != 0) {
            std::swap(oldPageWidth, oldPageHeight);
            std::swap(newPageWidth, newPageHeight);
        }
    } else {
        // Fallback for other document types
        int nativeWidth = effectiveNativeWidth();
        int nativeHeight = effectiveNativeHeight();
        oldPageWidth = static_cast<int>(nativeWidth * (oldScale / 100.0));
        oldPageHeight = static_cast<int>(nativeHeight * (oldScale / 100.0));
        newPageWidth = static_cast<int>(nativeWidth * (newScale / 100.0));
        newPageHeight = static_cast<int>(nativeHeight * (newScale / 100.0));
    }

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
    m_rotation = 0;          // Reset rotation to 0 degrees
    m_mirrorH = false;       // Reset horizontal mirroring
    m_mirrorV = false;       // Reset vertical mirroring
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

    // Update page dimensions based on effective size (accounting for downsampling)
    auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
    if (muPdfDoc) {
        m_pageWidth = muPdfDoc->getPageWidthEffective(m_currentPage, m_currentScale);
        m_pageHeight = muPdfDoc->getPageHeightEffective(m_currentPage, m_currentScale);
        
        // Apply rotation
        if (m_rotation % 180 != 0) {
            std::swap(m_pageWidth, m_pageHeight);
        }
    } else {
        // Fallback for other document types
        int nativeHeight = effectiveNativeHeight();
        m_pageWidth = static_cast<int>(nativeWidth * (m_currentScale / 100.0));
        m_pageHeight = static_cast<int>(nativeHeight * (m_currentScale / 100.0));
    }

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
                scrollingOccurred = true;
            }
        }
    }

    // --- HORIZONTAL edge → page turn ---
    const int maxX = getMaxScrollX();
    
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
            if (m_edgeTurnHoldRight > 0.0f || m_edgeTurnHoldLeft > 0.0f || m_edgeTurnHoldUp > 0.0f || m_edgeTurnHoldDown > 0.0f) {
                printf("DEBUG: Resetting edge-turn timers due to scroll timeout (timeout duration: %dms)\n", m_lastRenderDuration);
            }
            m_edgeTurnHoldRight = 0.0f;
            m_edgeTurnHoldLeft = 0.0f;
            m_edgeTurnHoldUp = 0.0f;
            m_edgeTurnHoldDown = 0.0f;
        }
    } else if (scrollingOccurred) {
        // Reset edge-turn timers if user is actively scrolling - only start timer when stationary at edge
        if (m_edgeTurnHoldRight > 0.0f || m_edgeTurnHoldLeft > 0.0f || m_edgeTurnHoldUp > 0.0f || m_edgeTurnHoldDown > 0.0f) {
            printf("DEBUG: Resetting edge-turn timers due to active scrolling\n");
        }
        m_edgeTurnHoldRight = 0.0f;
        m_edgeTurnHoldLeft = 0.0f;
        m_edgeTurnHoldUp = 0.0f;
        m_edgeTurnHoldDown = 0.0f;
    } else {
        // Only accumulate edge-turn time when not in scroll timeout AND not actively scrolling
        if (maxX == 0)
        {
            if (m_dpadRightHeld || m_keyboardRightHeld) {
                float oldTime = m_edgeTurnHoldRight;
                m_edgeTurnHoldRight += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldRight > 0.0f) {
                    printf("DEBUG: Right edge-turn timer started (maxX=0)\n");
                }
            } else {
                m_edgeTurnHoldRight = 0.0f;
            }
            if (m_dpadLeftHeld || m_keyboardLeftHeld) {
                float oldTime = m_edgeTurnHoldLeft;
                m_edgeTurnHoldLeft += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldLeft > 0.0f) {
                    printf("DEBUG: Left edge-turn timer started (maxX=0)\n");
                }
            } else {
                m_edgeTurnHoldLeft = 0.0f;
            }
        }
        else
        {
            // Use small tolerance for edge detection to handle rounding issues
            const int edgeTolerance = 2; // pixels
            
            if (m_scrollX <= (-maxX + edgeTolerance) && (m_dpadRightHeld || m_keyboardRightHeld)) {
                float oldTime = m_edgeTurnHoldRight;
                m_edgeTurnHoldRight += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldRight > 0.0f) {
                    printf("DEBUG: Right edge-turn timer started (scroll-based, scrollX=%d, threshold=%d)\n", m_scrollX, -maxX + edgeTolerance);
                }
            } else {
                if ((m_dpadRightHeld || m_keyboardRightHeld) && m_edgeTurnHoldRight > 0.0f) {
                    printf("DEBUG: Right edge-turn timer stopped (scrollX=%d, threshold=%d, held=%s)\n", 
                           m_scrollX, -maxX + edgeTolerance, (m_dpadRightHeld || m_keyboardRightHeld) ? "YES" : "NO");
                }
                m_edgeTurnHoldRight = 0.0f;
            }
            if (m_scrollX >= (maxX - edgeTolerance) && (m_dpadLeftHeld || m_keyboardLeftHeld)) {
                float oldTime = m_edgeTurnHoldLeft;
                m_edgeTurnHoldLeft += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldLeft > 0.0f) {
                    printf("DEBUG: Left edge-turn timer started (scroll-based, scrollX=%d, threshold=%d)\n", m_scrollX, maxX - edgeTolerance);
                }
            } else {
                if ((m_dpadLeftHeld || m_keyboardLeftHeld) && m_edgeTurnHoldLeft > 0.0f) {
                    printf("DEBUG: Left edge-turn timer stopped (scrollX=%d, threshold=%d, held=%s)\n", 
                           m_scrollX, maxX - edgeTolerance, (m_dpadLeftHeld || m_keyboardLeftHeld) ? "YES" : "NO");
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
            m_scrollX = getMaxScrollX(); // appear at left edge
            clampScroll();
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
            m_scrollX = -getMaxScrollX(); // appear at right edge
            clampScroll();
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
    const int maxY = getMaxScrollY();

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
            if (m_scrollY <= (-maxY + edgeTolerance) && (m_dpadDownHeld || m_keyboardDownHeld)) {
                float oldTime = m_edgeTurnHoldDown;
                m_edgeTurnHoldDown += dt;
                if (oldTime == 0.0f && m_edgeTurnHoldDown > 0.0f) {
                    printf("DEBUG: Down edge-turn timer started (scroll-based)\n");
                }
            } else {
                m_edgeTurnHoldDown = 0.0f;
            }

            // Top edge & still pushing up?
            if (m_scrollY >= (maxY - edgeTolerance) && (m_dpadUpHeld || m_keyboardUpHeld)) {
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
            m_scrollY = getMaxScrollY();
            clampScroll();
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
            m_scrollY = -getMaxScrollY();
            clampScroll();
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
    
    printf("DEBUG: Right nudge called - maxX=%d, scrollX=%d, condition=%s, inScrollTimeout=%s\n", 
           maxX, m_scrollX, (maxX == 0 || m_scrollX <= (-maxX + 2)) ? "AT_EDGE" : "NOT_AT_EDGE",
           isInScrollTimeout() ? "YES" : "NO");
    
    // Right nudge while already at right edge
    if (maxX == 0 || m_scrollX <= (-maxX + 2)) // Use same tolerance as edge-turn system
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
                    m_scrollX = getMaxScrollX(); // appear at left edge of new page
                    clampScroll();
                }
            }
        } else {
            // For zoomed pages (maxX > 0): defer to progress bar system
            printf("DEBUG: Zoomed page at edge - deferring to progress bar system\n");
        }
        return;
    }
    printf("DEBUG: Normal scroll - moving right\n");
    m_scrollX -= 50;
    clampScroll();
}

void App::handleDpadNudgeLeft()
{
    const int maxX = getMaxScrollX();
    
    printf("DEBUG: Left nudge called - maxX=%d, scrollX=%d, condition=%s\n", 
           maxX, m_scrollX, (maxX == 0 || m_scrollX >= (maxX - 2)) ? "AT_EDGE" : "NOT_AT_EDGE");
    
    // Left nudge while already at left edge
    if (maxX == 0 || m_scrollX >= (maxX - 2)) // Use same tolerance as edge-turn system
    {
        if (maxX == 0) {
            // Page fits horizontally (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldLeft == 0.0f) // Only if no progress bar is currently running
            {
                if (m_currentPage > 0 && !isInPageChangeCooldown())
                {
                    goToPreviousPage();
                    m_scrollX = -getMaxScrollX(); // appear at right edge of prev page
                    clampScroll();
                }
            }
        }
        // For zoomed pages (maxX > 0): always defer to progress bar system
        // This ensures a progress bar always appears when holding D-pad at edge
        return;
    }
    m_scrollX += 50;
    clampScroll();
}

void App::handleDpadNudgeDown()
{
    const int maxY = getMaxScrollY();
    
    printf("DEBUG: Down nudge called - maxY=%d, scrollY=%d, condition=%s\n", 
           maxY, m_scrollY, (maxY == 0 || m_scrollY <= (-maxY + 2)) ? "AT_EDGE" : "NOT_AT_EDGE");
    
    // Down nudge while already at bottom edge
    if (maxY == 0 || m_scrollY <= (-maxY + 2)) // Use same tolerance as edge-turn system
    {
        if (maxY == 0) {
            // Page fits vertically (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldDown == 0.0f) // Only if no progress bar is currently running
            {
                if (m_currentPage < m_pageCount - 1 && !isInPageChangeCooldown())
                {
                    goToNextPage();
                    m_scrollY = getMaxScrollY(); // appear at top edge of new page
                    clampScroll();
                }
            }
        }
        // For zoomed pages (maxY > 0): always defer to progress bar system
        // This ensures a progress bar always appears when holding D-pad at edge
        return;
    }
    m_scrollY -= 50;
    clampScroll();
}

void App::handleDpadNudgeUp()
{
    const int maxY = getMaxScrollY();
    
    printf("DEBUG: Up nudge called - maxY=%d, scrollY=%d, condition=%s\n", 
           maxY, m_scrollY, (maxY == 0 || m_scrollY >= (maxY - 2)) ? "AT_EDGE" : "NOT_AT_EDGE");
    
    // Up nudge while already at top edge
    if (maxY == 0 || m_scrollY >= (maxY - 2)) // Use same tolerance as edge-turn system
    {
        if (maxY == 0) {
            // Page fits vertically (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldUp == 0.0f) // Only if no progress bar is currently running
            {
                if (m_currentPage > 0 && !isInPageChangeCooldown())
                {
                    goToPreviousPage();
                    m_scrollY = -getMaxScrollY(); // appear at bottom edge of prev page
                    clampScroll();
                }
            }
        }
        // For zoomed pages (maxY > 0): always defer to progress bar system
        // This ensures a progress bar always appears when holding D-pad at edge
        return;
    }
    m_scrollY += 50;
    clampScroll();
}

void App::onPageChangedKeepZoom()
{
    // Predict scaled size for the new page using the current zoom
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

    // Guard against bad docs
    if (effectiveW <= 0 || effectiveH <= 0)
    {
        std::cerr << "App ERROR: Effective page dimensions are zero for page " << m_currentPage << std::endl;
        return;
    }

    m_pageWidth = effectiveW;
    m_pageHeight = effectiveH;

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
    
    std::cout << "DEBUG: Applying font config - " << config.fontName 
              << " at " << config.fontSize << "pt" << std::endl;
    
    // Generate CSS from the font configuration
    if (m_fontManager) {
        std::string css = m_fontManager->generateCSS(config);
        std::cout << "DEBUG: Generated CSS: " << css << std::endl;
        
        if (!css.empty()) {
            // Try to cast to MuPDF document and apply CSS
            if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get())) {
                muDoc->setUserCSS(css);
                
                // Save the configuration
                m_fontManager->saveConfig(config);
                
                // Force re-render of current page
                markDirty();
                
                // Close the font menu after successful application
                if (m_guiManager && m_guiManager->isFontMenuVisible()) {
                    m_guiManager->toggleFontMenu();
                }
                
                std::cout << "Applied font configuration: " << config.fontName 
                         << " at " << config.fontSize << "pt" << std::endl;
            } else {
                std::cout << "CSS styling not supported for this document type" << std::endl;
            }
        } else {
            std::cout << "Failed to generate CSS from font configuration" << std::endl;
        }
    } else {
        std::cout << "FontManager not available" << std::endl;
    }
}
