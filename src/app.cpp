#include "app.h"
#include "document.h"
#include "mupdf_document.h"
#include "navigation_manager.h"
#include "options_manager.h"
#include "renderer.h"
#include "text_renderer.h"
#ifdef TG5040_PLATFORM
#include "power_handler.h"
#endif

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

// --- App Class ---

// Constructor now accepts pre-initialized SDL_Window* and SDL_Renderer*
App::App(const std::string& filename, SDL_Window* window, SDL_Renderer* renderer)
    : m_running(true)
{

    // Store window and renderer for RenderManager initialization
    SDL_Window* localWindow = window;
    SDL_Renderer* localSDLRenderer = renderer;

#ifdef TG5040_PLATFORM
    // Initialize power handler
    m_powerHandler = std::make_unique<PowerHandler>();

    // Register error callback for displaying GUI messages
    m_powerHandler->setErrorCallback([this](const std::string& message)
                                     { showErrorMessage(message); });

    // Register sleep mode callback for fake sleep functionality
    m_powerHandler->setSleepModeCallback([this](bool enterFakeSleep)
                                         {
        m_inFakeSleep = enterFakeSleep;
        if (enterFakeSleep) {
            std::cout << "App: Entering fake sleep mode - disabling inputs, screen will go black" << std::endl;
            markDirty(); // Force screen redraw to show black screen
        } else {
            std::cout << "App: Exiting fake sleep mode - re-enabling inputs and screen" << std::endl;
            markDirty(); // Force screen redraw to restore normal display
        } });

    // Register pre-sleep callback to close UI windows before sleep
    m_powerHandler->setPreSleepCallback([this]() -> bool
                                        {
        if (m_guiManager) {
            bool anyClosed = m_guiManager->closeAllUIWindows();
            if (anyClosed) {
                std::cout << "App: Closed UI windows before sleep" << std::endl;
                // Don't show a brief flash - just close the UI
                // The power handler will immediately enter fake sleep mode (black screen)
                // and attempt real sleep, which is the proper behavior
                markDirty(); // Mark dirty so fake sleep black screen gets rendered
            }
            return anyClosed;
        }
        return false; });
#endif

    // Initialize font manager FIRST, before document creation
    m_optionsManager = std::make_unique<OptionsManager>();

    // Initialize reading history manager
    m_readingHistoryManager = std::make_unique<ReadingHistoryManager>();
    m_readingHistoryManager->loadHistory();

    // Store document path for reading history
    m_documentPath = filename;

    // Initialize navigation manager
    m_navigationManager = std::make_unique<NavigationManager>();

    // Initialize viewport manager (will be updated with proper renderer after RenderManager creation)
    m_viewportManager = std::make_unique<ViewportManager>(nullptr);

    // Load saved font configuration early and cache it
    FontConfig savedConfig = m_optionsManager->loadConfig();
    m_cachedConfig = savedConfig;

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
          lowercaseFilename.substr(lowercaseFilename.size() - 5) == ".mobi")))
    {
        m_document = std::make_unique<MuPdfDocument>();

        // IMPORTANT: Install custom font loader BEFORE opening document
        // This ensures fonts are available during initial document rendering
        if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get()))
        {
            m_optionsManager->installFontLoader(muDoc->getContext());
            std::cout << "DEBUG: Custom font loader installed before opening document" << std::endl;

            // Apply saved CSS configuration BEFORE opening document
            // Generate CSS even for "Document Default" to apply reading style colors
            std::string css = m_optionsManager->generateCSS(savedConfig);
            if (!css.empty())
            {
                muDoc->setUserCSSBeforeOpen(css);
                std::cout << "Applied saved CSS before opening document - Font: " << savedConfig.fontName
                          << ", Style: " << static_cast<int>(savedConfig.readingStyle) << std::endl;
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
        int windowWidth, windowHeight;
        SDL_GetWindowSize(localWindow, &windowWidth, &windowHeight);
        // Allow 4x zoom by setting max render size to 4x window size
        muDoc->setMaxRenderSize(windowWidth * 4, windowHeight * 4);
    }
#endif

    int pageCount = m_document->getPageCount();
    if (pageCount == 0)
    {
        throw std::runtime_error("Document contains no pages: " + filename);
    }

    // Set page count in navigation manager
    m_navigationManager->setPageCount(pageCount);

    // Check if we have a last read page for this document
    int lastPage = m_readingHistoryManager->getLastPage(m_documentPath);
    if (lastPage >= 0 && lastPage < pageCount)
    {
        m_navigationManager->setCurrentPage(lastPage);
        std::cout << "Restored last read page: " << (lastPage + 1) << " of " << pageCount << std::endl;
    }
    else
    {
        m_navigationManager->setCurrentPage(0);
    }

    // Initialize InputManager
    m_inputManager = std::make_unique<InputManager>();
    m_inputManager->setZoomStep(m_cachedConfig.zoomStep);
    m_inputManager->setPageCount(pageCount);

    // Note: Custom font loader is already installed before document opening
    // (see earlier in constructor, before m_document->open() call)

    // Initialize GUI manager AFTER font manager
    m_guiManager = std::make_unique<GuiManagerType>();
    if (!m_guiManager->initialize(window, renderer))
    {
        throw std::runtime_error("Failed to initialize GUI manager");
    }

    // Connect button mapper to GUI manager for platform-specific button handling
    m_guiManager->setButtonMapper(&m_inputManager->getButtonMapper());

    // Set up font apply callback AFTER all initialization is complete
    m_guiManager->setFontApplyCallback([this](const FontConfig& config)
                                       {
        std::cout << "DEBUG: Font apply callback triggered" << std::endl;
        applyFontConfiguration(config); });

    // Set up font close callback to trigger redraw
    m_guiManager->setFontCloseCallback([this]()
                                       {
                                           markDirty(); // Force redraw to clear menu
                                       });

    // Set up page jump callback
    m_guiManager->setPageJumpCallback([this](int pageNumber)
                                      {
        std::cout << "DEBUG: Page jump callback triggered to page " << (pageNumber + 1) << std::endl;
        m_navigationManager->goToPage(pageNumber, m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(),
                                      [this]() { markDirty(); },
                                      [this]() { updateScaleDisplayTime(); },
                                      [this]() { updatePageDisplayTime(); }); });

    // Initialize page information in GUI manager
    m_guiManager->setPageCount(m_navigationManager->getPageCount());
    m_guiManager->setCurrentPage(m_navigationManager->getCurrentPage());

    // Always set the saved configuration in GUI (even for Document Default)
    // This ensures reading style and font size are properly loaded
    m_guiManager->setCurrentFontConfig(savedConfig);
    std::cout << "Applied saved configuration: Font=" << savedConfig.fontName
              << ", Size=" << savedConfig.fontSize << "pt"
              << ", Style=" << static_cast<int>(savedConfig.readingStyle) << std::endl;

    // Initialize RenderManager LAST after all dependencies are ready
    m_renderManager = std::make_unique<RenderManager>(localWindow, localSDLRenderer);

    // Set initial background color based on reading style
    uint8_t bgR, bgG, bgB;
    OptionsManager::getReadingStyleBackgroundColor(savedConfig.readingStyle, bgR, bgG, bgB);
    m_renderManager->setBackgroundColor(bgR, bgG, bgB);
    m_renderManager->setShowMinimap(savedConfig.showDocumentMinimap);

    // Update ViewportManager with the proper renderer from RenderManager
    m_viewportManager->setRenderer(m_renderManager->getRenderer());

    // Now that ViewportManager has a valid renderer, do initial page load and fit
    loadDocument();
}

App::~App()
{
    std::cout.flush();
#ifdef TG5040_PLATFORM
    if (m_powerHandler)
    {
        std::cout.flush();
        m_powerHandler->stop();
        std::cout.flush();
    }
#endif
    // Explicitly destroy managers in controlled order to debug which one hangs
    std::cout.flush();
    m_renderManager.reset();

    std::cout.flush();
    m_navigationManager.reset();

    std::cout.flush();
    m_viewportManager.reset();

    std::cout.flush();
    m_inputManager.reset();

    std::cout.flush();
    m_optionsManager.reset();

    std::cout.flush();
    m_guiManager.reset();

    std::cout.flush();
    m_document.reset();

    std::cout.flush();
}

void App::run()
{
    m_prevTick = SDL_GetTicks();

#ifdef TG5040_PLATFORM
    // Start power button monitoring
    if (!m_powerHandler->start())
    {
        std::cerr << "Warning: Failed to start power button monitoring" << std::endl;
    }
#endif

    SDL_Event event;
    while (m_running)
    {
        // Always start GUI frame at the beginning of each main loop iteration
        // This ensures proper frame lifecycle management
        if (m_guiManager && !m_inFakeSleep)
        {
            m_guiManager->newFrame();
        }

        while (SDL_PollEvent(&event) != 0)
        {
            // In fake sleep mode, ignore all SDL events (power button is handled by PowerHandler)
            if (!m_inFakeSleep)
            {
                handleEvent(event);
            }
            else
            {
                // Only handle quit events to allow graceful shutdown
                if (event.type == SDL_QUIT)
                {
                    handleEvent(event);
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - m_prevTick) / 1000.0f;
        m_prevTick = now;

        if (!m_inFakeSleep)
        {
            // Normal rendering - only render if something changed
            bool panningChanged = updateHeldPanning(dt);

            // Apply pending zoom immediately for responsive feel
            // Don't wait for debouncing - apply incrementally
            if (m_viewportManager->hasPendingZoom())
            {
                m_viewportManager->applyPendingZoom(m_document.get(), m_navigationManager->getCurrentPage());
                markDirty();
            }

            // Apply pending font changes safely in the main loop
            if (m_pendingFontChange)
            {
                applyPendingFontChange();
            }

            // Enhanced frame pacing for TG5040: Skip rendering if we're rendering too frequently
            // This helps prevent warping during rapid input changes
            static Uint32 lastRenderTime = 0;
            Uint32 currentTime = SDL_GetTicks();

            // Force rendering if the font menu is visible, otherwise use normal logic
            bool shouldRender = false;
            if (m_guiManager && m_guiManager->isFontMenuVisible())
            {
                shouldRender = true; // Always render when font menu is visible
            }
            else
            {
                // Force render if marked dirty (e.g., after menu close) or other conditions
                shouldRender = (m_renderManager && m_renderManager->needsRedraw()) || panningChanged ||
                               ((currentTime - lastRenderTime) >= 16); // More aggressive rendering
            }

            bool doRender = false;

            if (m_viewportManager->isZoomDebouncing())
            {
                // During zoom processing, show processing indicator with minimal rendering
                // Re-render the current page at current scale with indicator overlay
                if ((currentTime - lastRenderTime) >= 100)
                { // Even slower update rate to minimize flicker
                    doRender = true;
                    // Important: Don't reset m_needsRedraw - preserve for final zoom render
                }
            }
            else if (shouldRender)
            {
                // Normal rendering when not processing zoom
                doRender = true;
            }

            if (doRender)
            {
                if (m_renderManager)
                {
                    m_renderManager->renderCurrentPage(m_document.get(), m_navigationManager.get(),
                                                       m_viewportManager.get(), m_documentMutex, m_isDragging);
                    m_renderManager->renderUI(this, m_navigationManager.get(), m_viewportManager.get());
                }

                // Always render GUI if we started a frame (which we always do when not in fake sleep)
                if (m_guiManager)
                {
                    m_guiManager->render();
                }

                if (m_renderManager)
                {
                    m_renderManager->present();
                }
                lastRenderTime = currentTime;

                // Only reset needsRedraw for normal rendering, not during zoom debouncing
                if (!m_viewportManager->isZoomDebouncing() && m_renderManager)
                {
                    m_renderManager->clearDirtyFlag();
                }
            }
            else
            {
                // Even if we don't render the main content, we must still finish the GUI frame
                // to maintain proper frame lifecycle
                if (m_guiManager)
                {
                    m_guiManager->render();
                }
            }
        }
        else
        {
            // Fake sleep mode - ALWAYS render black screen immediately
            // We must render every frame in fake sleep to ensure the screen stays black
            // even if we just transitioned from normal mode
            if (m_renderManager)
            {
                m_renderManager->renderFakeSleepScreen();
                m_renderManager->present(); // Must present the black screen to display!
            }
        }
    }
}

void App::handleEvent(const SDL_Event& event)
{
    // Let GUI handle the event first
    bool guiHandled = false;
    if (m_guiManager)
    {
        guiHandled = m_guiManager->handleEvent(event);
    }

    // If GUI handled the event (like button 10 to close menu), we're done
    if (guiHandled)
    {
        markDirty(); // Redraw to show menu state change
        return;
    }

    auto closeVisibleMenus = [this]()
    {
        if (m_guiManager)
        {
            if (m_guiManager->isFontMenuVisible())
            {
                m_guiManager->closeFontMenu();
            }
            if (m_guiManager->isNumberPadVisible())
            {
                m_guiManager->hideNumberPad();
            }
        }
    };

    // Block most input while menu overlays are visible, but allow key system controls
    if (m_guiManager && (m_guiManager->isFontMenuVisible() || m_guiManager->isNumberPadVisible()))
    {
        if (event.type == SDL_KEYDOWN)
        {
            switch (event.key.keysym.sym)
            {
            case SDLK_ESCAPE:
                closeVisibleMenus();
                markDirty();
                return;
            case SDLK_q:
                closeVisibleMenus();
                markDirty();
                m_running = false;
                return;
            case SDLK_m:
                m_guiManager->toggleFontMenu();
                markDirty();
                return;
            default:
                break;
            }
        }
        else if (event.type == SDL_QUIT)
        {
            closeVisibleMenus();
            m_running = false;
            return;
        }
        else if (event.type == SDL_CONTROLLERBUTTONDOWN ||
                 event.type == SDL_CONTROLLERBUTTONUP ||
                 event.type == SDL_JOYBUTTONDOWN ||
                 event.type == SDL_JOYBUTTONUP)
        {
            InputActionData actionData = m_inputManager->processEvent(event);
            if (actionData.action == InputAction::ToggleFontMenu)
            {
                m_guiManager->toggleFontMenu();
                markDirty();
                return;
            }
            if (actionData.action == InputAction::Quit)
            {
                closeVisibleMenus();
                markDirty();
                m_running = false;
                return;
            }
            return;
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
    switch (actionData.action)
    {
    case InputAction::Quit:
        m_running = false;
        break;
    case InputAction::Resize:
        m_viewportManager->fitPageToWindow(m_document.get(), m_navigationManager->getCurrentPage());
        markDirty();
        break;
    case InputAction::ToggleFontMenu:
        // Always toggle the menu (close if open, open if closed)
        toggleFontMenu();
        break;
    case InputAction::GoToNextPage:
        if (!m_navigationManager->isInPageChangeCooldown())
        {
            m_navigationManager->goToNextPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                              { markDirty(); }, [this]()
                                              { updateScaleDisplayTime(); }, [this]()
                                              {
                                                  updatePageDisplayTime();
                                                  // Save current page to reading history
                                                  m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
        }
        break;
    case InputAction::GoToPreviousPage:
        if (!m_navigationManager->isInPageChangeCooldown())
        {
            m_navigationManager->goToPreviousPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                                  { markDirty(); }, [this]()
                                                  { updateScaleDisplayTime(); }, [this]()
                                                  {
                                                      updatePageDisplayTime();
                                                      // Save current page to reading history
                                                      m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
        }
        break;
    case InputAction::ZoomIn:
        m_viewportManager->zoom(m_cachedConfig.zoomStep, m_document.get());
        updateScaleDisplayTime();
        markDirty();
        break;
    case InputAction::ZoomOut:
        m_viewportManager->zoom(-m_cachedConfig.zoomStep, m_document.get());
        updateScaleDisplayTime();
        markDirty();
        break;
    case InputAction::ZoomTo:
        m_viewportManager->zoomTo(actionData.intValue > 0 ? actionData.intValue : 100, m_document.get());
        updateScaleDisplayTime();
        markDirty();
        break;
    case InputAction::GoToFirstPage:
        m_navigationManager->goToPage(0, m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                      { markDirty(); }, [this]()
                                      { updateScaleDisplayTime(); }, [this]()
                                      {
                                          updatePageDisplayTime();
                                          m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
        break;
    case InputAction::GoToLastPage:
        m_navigationManager->goToPage(m_navigationManager->getPageCount() - 1, m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                      { markDirty(); }, [this]()
                                      { updateScaleDisplayTime(); }, [this]()
                                      {
                                          updatePageDisplayTime();
                                          m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
        break;
    case InputAction::GoToPage:
        if (actionData.intValue >= 0 && actionData.intValue < m_navigationManager->getPageCount())
        {
            m_navigationManager->goToPage(actionData.intValue, m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                          { markDirty(); }, [this]()
                                          { updateScaleDisplayTime(); }, [this]()
                                          {
                                              updatePageDisplayTime();
                                              m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
        }
        break;
    case InputAction::JumpPages:
        if (!m_navigationManager->isInPageChangeCooldown())
        {
            m_navigationManager->jumpPages(actionData.intValue, m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                           { markDirty(); }, [this]()
                                           { updateScaleDisplayTime(); }, [this]()
                                           {
                                               updatePageDisplayTime();
                                               m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
        }
        break;
    case InputAction::ToggleFullscreen:
        if (m_renderManager)
            m_renderManager->getRenderer()->toggleFullscreen();
        m_viewportManager->fitPageToWindow(m_document.get(), m_navigationManager->getCurrentPage());
        markDirty();
        break;
    case InputAction::StartPageJumpInput:
        m_navigationManager->startPageJumpInput();
        break;
    case InputAction::PrintAppState:
        printAppState();
        break;
    case InputAction::ClampScroll:
        m_viewportManager->clampScroll();
        break;
    case InputAction::FitPageToWidth:
        m_viewportManager->fitPageToWidth(m_document.get(), m_navigationManager->getCurrentPage());
        m_renderManager->clearLastRender(m_document.get()); // Clear preview cache to force re-render at new scale
        markDirty();
        break;
    case InputAction::FitPageToWindow:
        m_viewportManager->fitPageToWindow(m_document.get(), m_navigationManager->getCurrentPage());
        m_renderManager->clearLastRender(m_document.get()); // Clear preview cache to force re-render at new scale
        markDirty();
        break;
    case InputAction::ResetPageView:
        m_navigationManager->setCurrentPage(0);                // Reset to first page FIRST
        m_renderManager->clearLastRender(m_document.get());    // Clear any cached renders
        m_viewportManager->resetPageView(m_document.get(), 0); // Now reset viewport for page 0
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
        if (!m_navigationManager->isInScrollTimeout())
        {
            m_viewportManager->setScrollY(m_viewportManager->getScrollY() + static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::ScrollDown:
        if (!m_navigationManager->isInScrollTimeout())
        {
            m_viewportManager->setScrollY(m_viewportManager->getScrollY() - static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::MoveLeft:
        if (!m_navigationManager->isInScrollTimeout())
        {
            m_viewportManager->setScrollX(m_viewportManager->getScrollX() + static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::MoveRight:
        if (!m_navigationManager->isInScrollTimeout())
        {
            m_viewportManager->setScrollX(m_viewportManager->getScrollX() - static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::MoveUp:
        if (!m_navigationManager->isInScrollTimeout())
        {
            m_viewportManager->setScrollY(m_viewportManager->getScrollY() + static_cast<int>(actionData.floatValue));
            updatePageDisplayTime();
            m_viewportManager->clampScroll();
            markDirty();
        }
        break;
    case InputAction::MoveDown:
        if (!m_navigationManager->isInScrollTimeout())
        {
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
        if (m_isDragging && !m_navigationManager->isInScrollTimeout())
        {
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
        if (m_navigationManager->isPageJumpInputActive())
        {
            m_navigationManager->handlePageJumpInput(actionData.charValue);
        }
        break;
    case InputAction::ConfirmPageJumpInput:
        if (m_navigationManager->isPageJumpInputActive())
        {
            m_navigationManager->confirmPageJumpInput(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                                      { markDirty(); }, [this]()
                                                      { updateScaleDisplayTime(); }, [this]()
                                                      { updatePageDisplayTime(); }, [this](const std::string& message)
                                                      { showErrorMessage(message); });
        }
        break;
    case InputAction::CancelPageJumpInput:
        if (m_navigationManager->isPageJumpInputActive())
        {
            m_navigationManager->cancelPageJumpInput();
        }
        else if (m_guiManager && m_guiManager->isFontMenuVisible())
        {
            // Close font menu if it's open
            m_guiManager->toggleFontMenu();
            // Force redraw to clear the menu from screen
            markDirty();
        }
        else
        {
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
    switch (event.type)
    {
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym)
        {
        case SDLK_RIGHT:
            if (!m_keyboardRightHeld)
            { // Only on true initial press
                m_keyboardRightHeld = true;
                if (!m_navigationManager->isInScrollTimeout())
                {
                    handleDpadNudgeRight();
                    updatePageDisplayTime();
                    markDirty();
                }
            }
            break;
        case SDLK_LEFT:
            if (!m_keyboardLeftHeld)
            { // Only on true initial press
                m_keyboardLeftHeld = true;
                if (!m_navigationManager->isInScrollTimeout())
                {
                    handleDpadNudgeLeft();
                    updatePageDisplayTime();
                    markDirty();
                }
            }
            break;
        case SDLK_UP:
            if (!m_keyboardUpHeld)
            { // Only on true initial press
                m_keyboardUpHeld = true;
                if (!m_navigationManager->isInScrollTimeout())
                {
                    handleDpadNudgeUp();
                    updatePageDisplayTime();
                    markDirty();
                }
            }
            break;
        case SDLK_DOWN:
            if (!m_keyboardDownHeld)
            { // Only on true initial press
                m_keyboardDownHeld = true;
                if (!m_navigationManager->isInScrollTimeout())
                {
                    handleDpadNudgeDown();
                    updatePageDisplayTime();
                    markDirty();
                }
            }
            break;
        }
        break;

    case SDL_KEYUP:
        switch (event.key.keysym.sym)
        {
        case SDLK_RIGHT:
            m_keyboardRightHeld = false;
            if (m_edgeTurnHoldRight > 0.0f)
            {
                m_edgeTurnCooldownRight = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldRight = 0.0f;
            markDirty();
            break;
        case SDLK_LEFT:
            m_keyboardLeftHeld = false;
            if (m_edgeTurnHoldLeft > 0.0f)
            {
                m_edgeTurnCooldownLeft = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldLeft = 0.0f;
            markDirty();
            break;
        case SDLK_UP:
            m_keyboardUpHeld = false;
            if (m_edgeTurnHoldUp > 0.0f)
            {
                m_edgeTurnCooldownUp = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldUp = 0.0f;
            markDirty();
            break;
        case SDLK_DOWN:
            m_keyboardDownHeld = false;
            if (m_edgeTurnHoldDown > 0.0f)
            {
                m_edgeTurnCooldownDown = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldDown = 0.0f;
            markDirty();
            break;
        }
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

    case SDL_CONTROLLERBUTTONDOWN:
        // Process all controller button events - InputManager already validated the controller
        switch (event.cbutton.button)
        {
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (!m_dpadRightHeld)
            { // Only on true initial press
                m_dpadRightHeld = true;
                handleDpadNudgeRight();
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (!m_dpadLeftHeld)
            { // Only on true initial press
                m_dpadLeftHeld = true;
                handleDpadNudgeLeft();
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            if (!m_dpadUpHeld)
            { // Only on true initial press
                m_dpadUpHeld = true;
                handleDpadNudgeUp();
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (!m_dpadDownHeld)
            { // Only on true initial press
                m_dpadDownHeld = true;
                handleDpadNudgeDown();
            }
            break;
        }
        break;

    case SDL_CONTROLLERBUTTONUP:
        // Process all controller button events - InputManager already validated the controller
        switch (event.cbutton.button)
        {
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            m_dpadRightHeld = false;
            if (m_edgeTurnHoldRight > 0.0f)
            {
                m_edgeTurnCooldownRight = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldRight = 0.0f;
            markDirty();
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            m_dpadLeftHeld = false;
            if (m_edgeTurnHoldLeft > 0.0f)
            {
                m_edgeTurnCooldownLeft = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldLeft = 0.0f;
            markDirty();
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            m_dpadUpHeld = false;
            if (m_edgeTurnHoldUp > 0.0f)
            {
                m_edgeTurnCooldownUp = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldUp = 0.0f;
            markDirty();
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            m_dpadDownHeld = false;
            if (m_edgeTurnHoldDown > 0.0f)
            {
                m_edgeTurnCooldownDown = SDL_GetTicks() / 1000.0f;
            }
            m_edgeTurnHoldDown = 0.0f;
            markDirty();
            break;
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
}

void App::loadDocument()
{
    // Clear any cached renders from previous session/document
    m_renderManager->clearLastRender(m_document.get());

    // Don't reset page to 0 if it's already been set (e.g., from reading history)
    // Just fit the current page to window
    m_viewportManager->fitPageToWindow(m_document.get(), m_navigationManager->getCurrentPage());

    // Clear cache again after fitPageToWidth to ensure first render uses the correct scale
    // This is necessary because fitPageToWidth changes the scale and maxRenderSize
    m_renderManager->clearLastRender(m_document.get());

    // Ensure we start from the top-left corner so the restored page is fully visible
    // m_viewportManager->alignToTopOfCurrentPage();
    // m_viewportManager->setScrollX(m_viewportManager->getMaxScrollX());
    // m_viewportManager->clampScroll();
}

void App::applyPendingFontChange()
{
    if (!m_pendingFontChange)
    {
        return; // No pending font change
    }

    std::cout << "DEBUG: Applying pending font change - " << m_pendingFontConfig.fontName
              << " at " << m_pendingFontConfig.fontSize << "pt, style: "
              << static_cast<int>(m_pendingFontConfig.readingStyle) << std::endl;

    // Check if font, size, or style actually changed
    bool fontChanged = (m_pendingFontConfig.fontName != m_cachedConfig.fontName);
    bool sizeChanged = (m_pendingFontConfig.fontSize != m_cachedConfig.fontSize);
    bool styleChanged = (m_pendingFontConfig.readingStyle != m_cachedConfig.readingStyle);
    bool zoomStepChanged = (m_pendingFontConfig.zoomStep != m_cachedConfig.zoomStep);
    bool edgeProgressBarChanged = (m_pendingFontConfig.disableEdgeProgressBar != m_cachedConfig.disableEdgeProgressBar);
    bool minimapChanged = (m_pendingFontConfig.showDocumentMinimap != m_cachedConfig.showDocumentMinimap);

    if (!fontChanged && !sizeChanged && !styleChanged)
    {
        std::cout << "No font/size/style change detected - skipping document reopen" << std::endl;

        // Even if font/size/style didn't change, we still need to save other setting changes
        if (zoomStepChanged || edgeProgressBarChanged || minimapChanged)
        {
            std::cout << "Zoom step or edge progress bar changed - saving config" << std::endl;
            m_optionsManager->saveConfig(m_pendingFontConfig);
            refreshCachedConfig(); // Update cache after save

            if (zoomStepChanged)
            {
                m_inputManager->setZoomStep(m_pendingFontConfig.zoomStep);
            }
            if (m_renderManager)
            {
                m_renderManager->setShowMinimap(m_cachedConfig.showDocumentMinimap);
            }
        }

        // Just close the menu and mark for redraw
        if (m_guiManager && m_guiManager->isFontMenuVisible())
        {
            m_guiManager->toggleFontMenu();
        }
        markDirty();
        m_pendingFontChange = false;
        return;
    }

    // Generate CSS from the pending configuration
    if (m_optionsManager)
    {
        std::string css = m_optionsManager->generateCSS(m_pendingFontConfig);
        std::cout << "DEBUG: Generated CSS: " << css << std::endl;

        if (!css.empty())
        {
            // Try to cast to MuPDF document and apply CSS with safer reopening
            if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get()))
            {
                // Clear cache if font, size, or style changed (forces re-render with new styling)
                if (fontChanged || sizeChanged || styleChanged)
                {
                    std::cout << "Font, size, or style changed - clearing cache" << std::endl;
                    muDoc->clearCache();
                }

                // Store current state to restore after reopening
                int currentPage = m_navigationManager->getCurrentPage();
                int currentScale = m_viewportManager->getCurrentScale();
                int currentScrollX = m_viewportManager->getScrollX();
                int currentScrollY = m_viewportManager->getScrollY();

                // Use the much safer reopening method
                if (muDoc->reopenWithCSS(css))
                {
                    // Restore state after reopening with bounds checking
                    int pageCount = m_document->getPageCount();
                    if (currentPage >= 0 && currentPage < pageCount)
                    {
                        m_navigationManager->setCurrentPage(currentPage);
                    }
                    else
                    {
                        m_navigationManager->setCurrentPage(0); // Fallback to first page
                    }

                    // Restore scale with reasonable bounds
                    if (currentScale >= 10 && currentScale <= 350)
                    {
                        m_viewportManager->setCurrentScale(currentScale);
                    }
                    else
                    {
                        m_viewportManager->setCurrentScale(100); // Fallback to 100%
                    }

                    // Restore scroll position (will be clamped later)
                    m_viewportManager->setScrollX(currentScrollX);
                    m_viewportManager->setScrollY(currentScrollY);

                    // Update page count after reopening
                    m_navigationManager->setPageCount(pageCount);

                    // Update GUI manager's page count for the font menu display
                    if (m_guiManager)
                    {
                        m_guiManager->setPageCount(pageCount);
                    }

                    // Clamp scroll to ensure it's within bounds
                    m_viewportManager->clampScroll();

                    // Save the configuration
                    m_optionsManager->saveConfig(m_pendingFontConfig);

                    // Refresh cached config after saving
                    refreshCachedConfig();

                    if (m_renderManager)
                    {
                        m_renderManager->setShowMinimap(m_cachedConfig.showDocumentMinimap);
                    }

                    // Update InputManager's zoom step with the new value
                    m_inputManager->setZoomStep(m_pendingFontConfig.zoomStep);

                    // Update background color based on reading style
                    uint8_t bgR, bgG, bgB;
                    OptionsManager::getReadingStyleBackgroundColor(m_pendingFontConfig.readingStyle, bgR, bgG, bgB);
                    m_renderManager->setBackgroundColor(bgR, bgG, bgB);

                    // Force re-render of current page
                    markDirty();

                    // Close the font menu after successful application
                    if (m_guiManager && m_guiManager->isFontMenuVisible())
                    {
                        m_guiManager->toggleFontMenu();
                    }

                    std::cout << "Applied font configuration: " << m_pendingFontConfig.fontName
                              << " at " << m_pendingFontConfig.fontSize << "pt, style: "
                              << OptionsManager::getReadingStyleName(m_pendingFontConfig.readingStyle) << std::endl;
                }
                else
                {
                    std::cout << "Failed to reopen document with new CSS" << std::endl;
                }
            }
            else
            {
                std::cout << "CSS styling not supported for this document type" << std::endl;
            }
        }
        else
        {
            std::cout << "Failed to generate CSS from font configuration" << std::endl;
        }
    }
    else
    {
        std::cout << "FontManager not available" << std::endl;
    }

    // Clear the pending flag
    m_pendingFontChange = false;
}

// ---- helpers  ----

void App::printAppState()
{
    std::cout << "--- App State ---" << std::endl;
    std::cout << "Current Page: " << (m_navigationManager->getCurrentPage() + 1) << "/" << m_navigationManager->getPageCount() << std::endl;
    std::cout << "Native Page Dimensions: "
              << m_document->getPageWidthNative(m_navigationManager->getCurrentPage()) << "x"
              << m_document->getPageHeightNative(m_navigationManager->getCurrentPage()) << std::endl;
    std::cout << "Current Scale: " << m_viewportManager->getCurrentScale() << "%" << std::endl;
    std::cout << "Scaled Page Dimensions: " << m_viewportManager->getPageWidth() << "x" << m_viewportManager->getPageHeight() << " (Expected/Actual)" << std::endl;
    std::cout << "Scroll Position (Page Offset): X=" << m_viewportManager->getScrollX() << ", Y=" << m_viewportManager->getScrollY() << std::endl;
    if (m_renderManager)
    {
        std::cout << "Window Dimensions: " << m_renderManager->getRenderer()->getWindowWidth() << "x" << m_renderManager->getRenderer()->getWindowHeight() << std::endl;
    }

    // Also print navigation state
    m_navigationManager->printNavigationState();
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

    // Get the effective edge turn threshold based on cached configuration
    // If edge progress bar is disabled, use a very small threshold (0.001f) for instant page turns
    // We use 0.001f instead of 0.0f to avoid the condition timer >= threshold being always true
    // when both are 0.0f (since timer starts at 0.0f)
    // Otherwise, use the default threshold (0.300f)
    float effectiveEdgeTurnThreshold = m_cachedConfig.disableEdgeProgressBar ? 0.001f : m_edgeTurnThreshold;
    bool instantPageTurns = m_cachedConfig.disableEdgeProgressBar;

    float dx = 0.0f, dy = 0.0f;

    if (m_dpadLeftHeld || m_keyboardLeftHeld)
    {
        dx += 1.0f;
    }
    if (m_dpadRightHeld || m_keyboardRightHeld)
    {
        dx -= 1.0f;
    }
    if (m_dpadUpHeld || m_keyboardUpHeld)
    {
        dy += 1.0f;
    }
    if (m_dpadDownHeld || m_keyboardDownHeld)
    {
        dy -= 1.0f;
    }

    // Check if we're in scroll timeout after a page change
    bool inScrollTimeout = m_navigationManager->isInScrollTimeout();

    // Track if scrolling actually happened this frame
    bool scrollingOccurred = false;

    // Enhanced stability: Force a brief pause after page changes to prevent warping
    // This gives the rendering system time to stabilize before processing new input
    bool inStabilizationPeriod = m_navigationManager->isInScrollTimeout();

    if (dx != 0.0f || dy != 0.0f)
    {
        if (inScrollTimeout || inStabilizationPeriod)
        {
            // During scroll timeout or stabilization period, don't allow panning movement
            // This prevents scrolling past the beginning of a new page and reduces warping
            // But we still need to continue processing edge-turn logic below
        }
        else
        {
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
            if (dx != 0.0f && pixelMoveX == 0)
            {
                pixelMoveX = (dx > 0) ? 1 : -1;
            }
            if (dy != 0.0f && pixelMoveY == 0)
            {
                pixelMoveY = (dy > 0) ? 1 : -1;
            }

            m_viewportManager->setScrollX(m_viewportManager->getScrollX() + pixelMoveX);
            m_viewportManager->setScrollY(m_viewportManager->getScrollY() + pixelMoveY);
            m_viewportManager->clampScroll();

            if (m_viewportManager->getScrollX() != oldScrollX || m_viewportManager->getScrollY() != oldScrollY)
            {
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
    if (inScrollTimeout || inStabilizationPeriod)
    {
        // During stabilization period, gradually decay edge-turn timers instead of hard reset
        // This provides smoother visual feedback and reduces warping appearance
        if (inStabilizationPeriod && !inScrollTimeout)
        {
            // Gradual decay during stabilization period (but not timeout)
            float decayFactor = 0.95f; // Decay 5% per frame
            m_edgeTurnHoldRight *= decayFactor;
            m_edgeTurnHoldLeft *= decayFactor;
            m_edgeTurnHoldUp *= decayFactor;
            m_edgeTurnHoldDown *= decayFactor;

            // Reset to zero when very small to avoid floating point drift
            if (m_edgeTurnHoldRight < 0.01f)
                m_edgeTurnHoldRight = 0.0f;
            if (m_edgeTurnHoldLeft < 0.01f)
                m_edgeTurnHoldLeft = 0.0f;
            if (m_edgeTurnHoldUp < 0.01f)
                m_edgeTurnHoldUp = 0.0f;
            if (m_edgeTurnHoldDown < 0.01f)
                m_edgeTurnHoldDown = 0.0f;
        }
        else
        {
            // Hard reset during scroll timeout
            m_edgeTurnHoldRight = 0.0f;
            m_edgeTurnHoldLeft = 0.0f;
            m_edgeTurnHoldUp = 0.0f;
            m_edgeTurnHoldDown = 0.0f;
        }
    }
    else if (scrollingOccurred)
    {
        // Reset edge-turn timers if user is actively scrolling - only start timer when stationary at edge
        m_edgeTurnHoldRight = 0.0f;
        m_edgeTurnHoldLeft = 0.0f;
        m_edgeTurnHoldUp = 0.0f;
        m_edgeTurnHoldDown = 0.0f;
    }
    else
    {
        // Only accumulate edge-turn time when not in scroll timeout AND not actively scrolling
        if (maxX == 0)
        {
            if (m_dpadRightHeld || m_keyboardRightHeld)
            {
                // In instant mode, set to threshold immediately, don't keep accumulating
                if (instantPageTurns && m_edgeTurnHoldRight == 0.0f)
                {
                    m_edgeTurnHoldRight = effectiveEdgeTurnThreshold;
                }
                else if (!instantPageTurns)
                {
                    m_edgeTurnHoldRight += dt;
                }
            }
            else
            {
                m_edgeTurnHoldRight = 0.0f;
            }
            if (m_dpadLeftHeld || m_keyboardLeftHeld)
            {
                // In instant mode, set to threshold immediately, don't keep accumulating
                if (instantPageTurns && m_edgeTurnHoldLeft == 0.0f)
                {
                    m_edgeTurnHoldLeft = effectiveEdgeTurnThreshold;
                }
                else if (!instantPageTurns)
                {
                    m_edgeTurnHoldLeft += dt;
                }
            }
            else
            {
                m_edgeTurnHoldLeft = 0.0f;
            }
        }
        else
        {
            // Use small tolerance for edge detection to handle rounding issues
            const int edgeTolerance = 2; // pixels

            if (m_viewportManager->getScrollX() <= (-maxX + edgeTolerance) && (m_dpadRightHeld || m_keyboardRightHeld))
            {
                // In instant mode, set to threshold immediately on first frame, don't keep accumulating
                if (instantPageTurns && m_edgeTurnHoldRight == 0.0f)
                {
                    m_edgeTurnHoldRight = effectiveEdgeTurnThreshold;
                }
                else if (!instantPageTurns)
                {
                    m_edgeTurnHoldRight += dt;
                }
            }
            else
            {
                m_edgeTurnHoldRight = 0.0f;
            }
            if (m_viewportManager->getScrollX() >= (maxX - edgeTolerance) && (m_dpadLeftHeld || m_keyboardLeftHeld))
            {
                // In instant mode, set to threshold immediately on first frame, don't keep accumulating
                if (instantPageTurns && m_edgeTurnHoldLeft == 0.0f)
                {
                    m_edgeTurnHoldLeft = effectiveEdgeTurnThreshold;
                }
                else if (!instantPageTurns)
                {
                    m_edgeTurnHoldLeft += dt;
                }
            }
            else
            {
                if ((m_dpadLeftHeld || m_keyboardLeftHeld) && m_edgeTurnHoldLeft > 0.0f)
                {
                    printf("DEBUG: Left edge-turn timer stopped (scrollX=%d, threshold=%d, held=%s)\n",
                           m_viewportManager->getScrollX(), maxX - edgeTolerance, (m_dpadLeftHeld || m_keyboardLeftHeld) ? "YES" : "NO");
                }
                m_edgeTurnHoldLeft = 0.0f;
            }
        }
    }

    if (m_edgeTurnHoldRight >= effectiveEdgeTurnThreshold)
    {
        // Check cooldown before allowing page change
        float currentTime = SDL_GetTicks() / 1000.0f;
        bool inCooldown = (m_edgeTurnCooldownRight > 0.0f) &&
                          (currentTime - m_edgeTurnCooldownRight < m_edgeTurnCooldownDuration);

        if (!inCooldown && m_navigationManager->getCurrentPage() < m_navigationManager->getPageCount() - 1 && !m_navigationManager->isInPageChangeCooldown())
        {
            m_navigationManager->goToNextPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                              { markDirty(); }, [this]()
                                              { updateScaleDisplayTime(); }, [this]()
                                              {
                                                  updatePageDisplayTime();
                                                  m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
            m_viewportManager->setScrollX(m_viewportManager->getMaxScrollX()); // appear at left edge
            m_viewportManager->clampScroll();
            // Set cooldown timestamp to prevent immediate re-triggering while button is still held
            m_edgeTurnCooldownRight = SDL_GetTicks() / 1000.0f;
            changed = true;
        }
        m_edgeTurnHoldRight = 0.0f;
    }
    else if (m_edgeTurnHoldLeft >= effectiveEdgeTurnThreshold)
    {
        // Check cooldown before allowing page change
        float currentTime = SDL_GetTicks() / 1000.0f;
        bool inCooldown = (m_edgeTurnCooldownLeft > 0.0f) &&
                          (currentTime - m_edgeTurnCooldownLeft < m_edgeTurnCooldownDuration);

        if (!inCooldown && m_navigationManager->getCurrentPage() > 0 && !m_navigationManager->isInPageChangeCooldown())
        {
            m_navigationManager->goToPreviousPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                                  { markDirty(); }, [this]()
                                                  { updateScaleDisplayTime(); }, [this]()
                                                  {
                                                      updatePageDisplayTime();
                                                      m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
            m_viewportManager->setScrollX(-m_viewportManager->getMaxScrollX()); // appear at right edge
            m_viewportManager->clampScroll();
            // Set cooldown timestamp to prevent immediate re-triggering while button is still held
            m_edgeTurnCooldownLeft = SDL_GetTicks() / 1000.0f;
            changed = true;
        }
        m_edgeTurnHoldLeft = 0.0f;
    }

    // --- VERTICAL edge → page turn (NEW) ---
    const int maxY = m_viewportManager->getMaxScrollY();

    if (!inScrollTimeout && !scrollingOccurred)
    {
        // Only accumulate edge-turn time when not in scroll timeout AND not actively scrolling
        if (maxY == 0)
        {
            // Page fits vertically: treat sustained up/down as page turns
            if (m_dpadDownHeld || m_keyboardDownHeld)
            {
                // In instant mode, set to threshold immediately on first frame, don't keep accumulating
                if (instantPageTurns && m_edgeTurnHoldDown == 0.0f)
                {
                    m_edgeTurnHoldDown = effectiveEdgeTurnThreshold;
                }
                else if (!instantPageTurns)
                {
                    m_edgeTurnHoldDown += dt;
                }
            }
            else
            {
                m_edgeTurnHoldDown = 0.0f;
            }
            if (m_dpadUpHeld || m_keyboardUpHeld)
            {
                // In instant mode, set to threshold immediately on first frame, don't keep accumulating
                if (instantPageTurns && m_edgeTurnHoldUp == 0.0f)
                {
                    m_edgeTurnHoldUp = effectiveEdgeTurnThreshold;
                }
                else if (!instantPageTurns)
                {
                    m_edgeTurnHoldUp += dt;
                }
            }
            else
            {
                m_edgeTurnHoldUp = 0.0f;
            }
        }
        else
        {
            // Use small tolerance for edge detection to handle rounding issues
            const int edgeTolerance = 2; // pixels

            // Bottom edge & still pushing down? (down moves view further down in your scheme: dy < 0)
            if (m_viewportManager->getScrollY() <= (-maxY + edgeTolerance) && (m_dpadDownHeld || m_keyboardDownHeld))
            {
                // In instant mode, set to threshold immediately on first frame, don't keep accumulating
                if (instantPageTurns && m_edgeTurnHoldDown == 0.0f)
                {
                    m_edgeTurnHoldDown = effectiveEdgeTurnThreshold;
                }
                else if (!instantPageTurns)
                {
                    m_edgeTurnHoldDown += dt;
                }
            }
            else
            {
                m_edgeTurnHoldDown = 0.0f;
            }

            // Top edge & still pushing up?
            if (m_viewportManager->getScrollY() >= (maxY - edgeTolerance) && (m_dpadUpHeld || m_keyboardUpHeld))
            {
                // In instant mode, set to threshold immediately on first frame, don't keep accumulating
                if (instantPageTurns && m_edgeTurnHoldUp == 0.0f)
                {
                    m_edgeTurnHoldUp = effectiveEdgeTurnThreshold;
                }
                else if (!instantPageTurns)
                {
                    m_edgeTurnHoldUp += dt;
                }
            }
            else
            {
                m_edgeTurnHoldUp = 0.0f;
            }
        }
    }
    else if (scrollingOccurred)
    {
        // Reset vertical edge-turn timers if actively scrolling
        if (m_edgeTurnHoldUp > 0.0f || m_edgeTurnHoldDown > 0.0f)
        {
            printf("DEBUG: Resetting vertical edge-turn timers due to active scrolling\n");
        }
        m_edgeTurnHoldUp = 0.0f;
        m_edgeTurnHoldDown = 0.0f;
    }

    if (m_edgeTurnHoldDown >= effectiveEdgeTurnThreshold)
    {
        // Check cooldown before allowing page change
        float currentTime = SDL_GetTicks() / 1000.0f;
        bool inCooldown = (m_edgeTurnCooldownDown > 0.0f) &&
                          (currentTime - m_edgeTurnCooldownDown < m_edgeTurnCooldownDuration);

        if (!inCooldown && m_navigationManager->getCurrentPage() < m_navigationManager->getPageCount() - 1 && !m_navigationManager->isInPageChangeCooldown())
        {
            m_navigationManager->goToNextPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                              { markDirty(); }, [this]()
                                              { updateScaleDisplayTime(); }, [this]()
                                              {
                                                  updatePageDisplayTime();
                                                  m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
            // Land at the top edge of the new page so motion feels continuous downward
            m_viewportManager->setScrollY(m_viewportManager->getMaxScrollY());
            m_viewportManager->clampScroll();
            // Set cooldown timestamp to prevent immediate re-triggering while button is still held
            m_edgeTurnCooldownDown = SDL_GetTicks() / 1000.0f;
            changed = true;
        }
        m_edgeTurnHoldDown = 0.0f;
    }
    else if (m_edgeTurnHoldUp >= effectiveEdgeTurnThreshold)
    {
        // Check cooldown before allowing page change
        float currentTime = SDL_GetTicks() / 1000.0f;
        bool inCooldown = (m_edgeTurnCooldownUp > 0.0f) &&
                          (currentTime - m_edgeTurnCooldownUp < m_edgeTurnCooldownDuration);

        if (!inCooldown && m_navigationManager->getCurrentPage() > 0 && !m_navigationManager->isInPageChangeCooldown())
        {
            m_navigationManager->goToPreviousPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                                  { markDirty(); }, [this]()
                                                  { updateScaleDisplayTime(); }, [this]()
                                                  {
                                                      updatePageDisplayTime();
                                                      m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
            // Land at the bottom edge of the previous page
            m_viewportManager->setScrollY(-m_viewportManager->getMaxScrollY());
            m_viewportManager->clampScroll();
            // Set cooldown timestamp to prevent immediate re-triggering while button is still held
            m_edgeTurnCooldownUp = SDL_GetTicks() / 1000.0f;
            changed = true;
        }
        m_edgeTurnHoldUp = 0.0f;
    }

    // Check if any edge-turn timing values changed and mark as dirty for progress indicator updates
    if (m_edgeTurnHoldRight != oldEdgeTurnHoldRight ||
        m_edgeTurnHoldLeft != oldEdgeTurnHoldLeft ||
        m_edgeTurnHoldUp != oldEdgeTurnHoldUp ||
        m_edgeTurnHoldDown != oldEdgeTurnHoldDown)
    {
        markDirty();
    }

    return changed;
}

void App::handleDpadNudgeRight()
{
    const int maxX = m_viewportManager->getMaxScrollX();

    printf("DEBUG: Right nudge called - maxX=%d, scrollX=%d, condition=%s, inScrollTimeout=%s\n",
           maxX, m_viewportManager->getScrollX(), (maxX == 0 || m_viewportManager->getScrollX() <= (-maxX + 2)) ? "AT_EDGE" : "NOT_AT_EDGE",
           m_navigationManager->isInScrollTimeout() ? "YES" : "NO");

    // Right nudge while already at right edge
    if (maxX == 0 || m_viewportManager->getScrollX() <= (-maxX + 2)) // Use same tolerance as edge-turn system
    {
        if (maxX == 0)
        {
            // Page fits horizontally (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldRight == 0.0f) // Only if no progress bar is currently running
            {
                if (m_navigationManager->getCurrentPage() < m_navigationManager->getPageCount() - 1 && !m_navigationManager->isInPageChangeCooldown())
                {
                    printf("DEBUG: Immediate page change via nudge (fit-to-width)\n");
                    m_navigationManager->goToNextPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                                      { markDirty(); }, [this]()
                                                      { updateScaleDisplayTime(); }, [this]()
                                                      {
                                                          updatePageDisplayTime();
                                                          m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
                    m_viewportManager->setScrollX(m_viewportManager->getMaxScrollX()); // appear at left edge of new page
                    m_viewportManager->clampScroll();
                }
            }
        }
        else
        {
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
        if (maxX == 0)
        {
            // Page fits horizontally (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldLeft == 0.0f) // Only if no progress bar is currently running
            {
                if (m_navigationManager->getCurrentPage() > 0 && !m_navigationManager->isInPageChangeCooldown())
                {
                    m_navigationManager->goToPreviousPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                                          { markDirty(); }, [this]()
                                                          { updateScaleDisplayTime(); }, [this]()
                                                          {
                                                              updatePageDisplayTime();
                                                              m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
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
        if (maxY == 0)
        {
            // Page fits vertically (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldDown == 0.0f) // Only if no progress bar is currently running
            {
                if (m_navigationManager->getCurrentPage() < m_navigationManager->getPageCount() - 1 && !m_navigationManager->isInPageChangeCooldown())
                {
                    m_navigationManager->goToNextPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                                      { markDirty(); }, [this]()
                                                      { updateScaleDisplayTime(); }, [this]()
                                                      {
                                                          updatePageDisplayTime();
                                                          m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
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
        if (maxY == 0)
        {
            // Page fits vertically (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldUp == 0.0f) // Only if no progress bar is currently running
            {
                if (m_navigationManager->getCurrentPage() > 0 && !m_navigationManager->isInPageChangeCooldown())
                {
                    m_navigationManager->goToPreviousPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                                          { markDirty(); }, [this]()
                                                          { updateScaleDisplayTime(); }, [this]()
                                                          {
                                                              updatePageDisplayTime();
                                                              m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
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

// Utility methods moved to convenience methods in header

void App::toggleFontMenu()
{
    if (m_guiManager)
    {
        m_guiManager->toggleFontMenu();
        markDirty(); // Force redraw to show/hide the menu
    }
}

void App::applyFontConfiguration(const FontConfig& config)
{
    if (!m_document)
    {
        std::cerr << "Cannot apply font configuration: no document loaded" << std::endl;
        return;
    }

    std::cout << "DEBUG: Scheduling deferred font change - " << config.fontName
              << " at " << config.fontSize << "pt" << std::endl;

    // Cancel any ongoing prerendering to speed up font application
    if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get()))
    {
        muDoc->cancelPrerendering();
    }

    // Store the configuration for deferred processing in the main loop
    m_pendingFontConfig = config;
    m_pendingFontChange = true;

    // The actual document reopening will happen safely in the main loop
    // via applyPendingFontChange()
}
std::function<void(int)> App::makeSetCurrentPageCallback()
{
    return [this](int page)
    {
        if (m_guiManager)
        {
            m_guiManager->setCurrentPage(page);
        }
    };
}
