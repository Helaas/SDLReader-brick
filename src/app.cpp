#include "app.h"
#include "document.h"
#include "mupdf_document.h"
#include "text_document.h"
#include "navigation_manager.h"
#include "options_manager.h"
#include "renderer.h"
#include "supported_file_types.h"
#include "text_renderer.h"
#if defined(TRIMUI_PLATFORM) || defined(PLATFORM_MLP1)
#include "power_handler.h"
#include "power_events.h"
#endif
#include "platform_constants.h"
#ifdef PLATFORM_MY355
#include "platform_my355.h"
#endif

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

// --- App Class ---

// Constructor now accepts pre-initialized SDL_Window* and SDL_Renderer*
App::App(const std::string& filename, SDL_Window* window, SDL_Renderer* renderer, AppLaunchOptions launchOptions)
    : m_running(true), m_launchOptions(launchOptions)
{

    // Store window and renderer for RenderManager initialization
    SDL_Window* localWindow = window;
    SDL_Renderer* localSDLRenderer = renderer;

#if defined(TRIMUI_PLATFORM)
    // Initialize power handler
    m_powerHandler = std::make_unique<PowerHandler>();

    m_powerMessageEventType = getPowerMessageEventType();

    // Register error callback for displaying GUI messages
    m_powerHandler->setErrorCallback([this](const std::string& message)
                                     {
        SDL_Event event;
        SDL_zero(event);
        event.type = m_powerMessageEventType;
        event.user.code = 0;
        event.user.data1 = new std::string(message);
        event.user.data2 = nullptr;
        if (SDL_PushEvent(&event) < 0)
        {
            delete static_cast<std::string*>(event.user.data1);
            std::cerr << "App: Failed to push power message event: " << SDL_GetError() << std::endl;
        } });

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
#elif defined(PLATFORM_MLP1)
    // Initialize MLP1 power handler (read-only observer mode)
    // On MLP1, loong_power daemon owns the power button. SDLReader's handler
    // watches for long-press shutdown only. Short-press suspend is delegated.
    m_powerHandler = std::make_unique<PowerHandler>();

    m_powerMessageEventType = getPowerMessageEventType();

    m_powerHandler->setErrorCallback([this](const std::string& message)
                                     {
        SDL_Event event;
        SDL_zero(event);
        event.type = m_powerMessageEventType;
        event.user.code = 0;
        event.user.data1 = new std::string(message);
        event.user.data2 = nullptr;
        if (SDL_PushEvent(&event) < 0)
        {
            delete static_cast<std::string*>(event.user.data1);
            std::cerr << "App: Failed to push power message event: " << SDL_GetError() << std::endl;
        } });

    // MLP1: no fake sleep mode; loong_power handles screen blanking
    m_powerHandler->setSleepModeCallback([this](bool) {});

    m_powerHandler->setPreSleepCallback([this]() -> bool
                                        {
        if (m_guiManager) {
            return m_guiManager->closeAllUIWindows();
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

    // Apply saved settings to navigation manager
    m_navigationManager->setKeepPanningPosition(savedConfig.keepPanningPosition);

    const SupportedFileTypes::DocumentKind documentKind = SupportedFileTypes::classifyDocumentPath(filename);

    if (documentKind == SupportedFileTypes::DocumentKind::Text)
    {
        auto txtDoc = std::make_unique<TextDocument>();
        txtDoc->setFontConfig(savedConfig);
        m_document = std::move(txtDoc);
    }
    else if (documentKind == SupportedFileTypes::DocumentKind::MuPdf)
    {
        m_document = std::make_unique<MuPdfDocument>();

        // IMPORTANT: Install custom font loader BEFORE opening document
        // This ensures fonts are available during initial document rendering
        if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get()))
        {
            m_optionsManager->installFontLoader(muDoc->getContext());

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
                                 " (supported: " + SupportedFileTypes::getSupportedExtensionList() + ")");
    }

    if (!m_document->open(filename))
    {
        throw std::runtime_error("Failed to open document: " + filename);
    }

#ifndef TRIMUI_PLATFORM
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

    int lastPage = m_readingHistoryManager->getLastPage(m_documentPath);

    if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get()))
    {
        if (lastPage >= 0)
        {
            muDoc->ensurePageCountAtLeast(lastPage + 1);
        }
    }

    int pageCount = m_document->getPageCount();
    bool pageCountEstimated = false;
    if (auto muDoc = dynamic_cast<MuPdfDocument*>(m_document.get()))
    {
        pageCountEstimated = !muDoc->isPageCountFinal() && muDoc->isPageCountEstimated();
    }
    if (pageCount == 0)
    {
        throw std::runtime_error("Document contains no pages: " + filename);
    }

    int navigationPageCount = pageCount;
    if (lastPage >= 0 && (lastPage + 1) > navigationPageCount)
    {
        navigationPageCount = lastPage + 1;
    }

    // Set page count in navigation manager
    m_navigationManager->setPageCount(navigationPageCount);
    m_navigationManager->setDisplayPageCount(navigationPageCount, pageCountEstimated);

    // Check if we have a last read page for this document
    if (lastPage >= 0 && lastPage < navigationPageCount)
    {
        m_navigationManager->setCurrentPage(lastPage);
        std::cout << "Restored last read page: " << (lastPage + 1) << " of " << navigationPageCount << std::endl;
    }
    else
    {
        m_navigationManager->setCurrentPage(0);
    }

    // Initialize InputManager
    m_inputManager = std::make_unique<InputManager>();
    m_inputManager->setZoomStep(m_cachedConfig.zoomStep);
    m_inputManager->setPageCount(navigationPageCount);

    // Note: Custom font loader is already installed before document opening
    // (see earlier in constructor, before m_document->open() call)

    // Initialize GUI manager AFTER font manager
    m_guiManager = std::make_unique<GuiManagerType>();
    if (!m_guiManager->initialize(window, renderer))
    {
        throw std::runtime_error("Failed to initialize GUI manager");
    }
    m_guiManager->setShowFileBrowserImageSettingVisible(!m_launchOptions.forceShowImagesInFileBrowser);

    // Connect button mapper to GUI manager for platform-specific button handling
    m_guiManager->setButtonMapper(&m_inputManager->getButtonMapper());

    // Set up font apply callback AFTER all initialization is complete
    m_guiManager->setFontApplyCallback([this](const FontConfig& config)
                                       { applyFontConfiguration(config); });

    // Set up font close callback to trigger redraw
    m_guiManager->setFontCloseCallback([this]()
                                       {
                                           markDirty(); // Force redraw to clear menu
                                       });

    // Set up page jump callback
    m_guiManager->setPageJumpCallback([this](int pageNumber)
                                      { m_navigationManager->goToPage(pageNumber, m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                                                      { markDirty(); }, [this]()
                                                                      { updateScaleDisplayTime(); }, [this]()
                                                                      { updatePageDisplayTime(); }); });

    // Initialize page information in GUI manager
    m_guiManager->setPageCount(m_navigationManager->getDisplayPageCount(), pageCountEstimated);
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
    m_renderManager->setShowPageIndicatorOverlay(savedConfig.showPageIndicatorOverlay);
    m_renderManager->setShowScaleOverlay(savedConfig.showScaleOverlay);

    // Update ViewportManager with the proper renderer from RenderManager
    m_viewportManager->setRenderer(m_renderManager->getRenderer());

    // Now that ViewportManager has a valid renderer, do initial page load and fit
    loadDocument();
}

App::~App()
{
#if defined(TRIMUI_PLATFORM) || defined(PLATFORM_MLP1)
    if (m_powerHandler)
    {
        m_powerHandler->stop();
    }
#endif
    m_renderManager.reset();
    m_navigationManager.reset();
    m_viewportManager.reset();
    m_inputManager.reset();
    m_guiManager.reset();
    m_document.reset();
    m_optionsManager.reset();
}

void App::run()
{
    m_prevTick = SDL_GetTicks();

#if defined(TRIMUI_PLATFORM) || defined(PLATFORM_MLP1)
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

        refreshPageCountFromDocument();

        while (SDL_PollEvent(&event) != 0)
        {
#if defined(TRIMUI_PLATFORM) || defined(PLATFORM_MLP1)
            if (m_powerMessageEventType != 0 && event.type == m_powerMessageEventType)
            {
                handlePowerMessageEvent(event);
                continue;
            }
#endif
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

#if defined(TRIMUI_PLATFORM) || defined(PLATFORM_MLP1)
void App::handlePowerMessageEvent(const SDL_Event& event)
{
    std::unique_ptr<std::string> message(static_cast<std::string*>(event.user.data1));
    if (!message)
    {
        return;
    }

    if (m_guiManager && m_guiManager->closeAllUIWindows())
    {
        markDirty();
    }

    showErrorMessage(*message);
}
#endif

void App::handleEvent(const SDL_Event& event)
{
#ifdef PLATFORM_MY355
    // my355 emits physical buttons as both keyboard scancodes and controller events.
    // Ignore keyboard-side button scancodes here to prevent double-processing in UI/app layers.
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
        isMy355ButtonScancode(event.key.keysym.scancode))
    {
        return;
    }
#endif

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

    auto closeVisibleMenus = [this]() -> bool
    {
        if (m_guiManager)
        {
            return m_guiManager->closeTopUIWindow();
        }
        return false;
    };

    // Block most input while menu overlays are visible, but allow key system controls
    if (m_guiManager && (m_guiManager->isFontMenuVisible() || m_guiManager->isNumberPadVisible()))
    {
        if (event.type == SDL_KEYDOWN)
        {
            switch (event.key.keysym.sym)
            {
            case SDLK_ESCAPE:
            {
                bool closed = closeVisibleMenus();
                if (closed)
                {
                    markDirty();
                    return;
                }
                break;
            }
            case SDLK_q:
            {
                bool closed = closeVisibleMenus();
                if (closed)
                {
                    markDirty();
                    return;
                }
                m_running = false;
                return;
            }
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
            bool closed = closeVisibleMenus();
            if (closed)
            {
                markDirty();
                return;
            }
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
                bool closed = closeVisibleMenus();
                if (closed)
                {
                    markDirty();
                }
                else
                {
                    m_running = false;
                }
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
        // Re-apply the current fit mode to adapt to new window dimensions
        m_viewportManager->applyFitMode(m_document.get(), m_navigationManager->getCurrentPage());
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
        updateScaleDisplayTime();
        markDirty();
        break;
    case InputAction::FitPageToWindow:
        m_viewportManager->fitPageToWindow(m_document.get(), m_navigationManager->getCurrentPage());
        m_renderManager->clearLastRender(m_document.get()); // Clear preview cache to force re-render at new scale
        updateScaleDisplayTime();
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
        m_viewportManager->rotateClockwise(m_document.get(), m_navigationManager->getCurrentPage());
        updateScaleDisplayTime();
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
            resetEdgeTurnProgressForDirection(EdgeDirection::Right, true);
            armDoubleTapEdgeTurnDirection(EdgeDirection::Right);
            markDirty();
            break;
        case SDLK_LEFT:
            m_keyboardLeftHeld = false;
            resetEdgeTurnProgressForDirection(EdgeDirection::Left, true);
            armDoubleTapEdgeTurnDirection(EdgeDirection::Left);
            markDirty();
            break;
        case SDLK_UP:
            m_keyboardUpHeld = false;
            resetEdgeTurnProgressForDirection(EdgeDirection::Up, true);
            armDoubleTapEdgeTurnDirection(EdgeDirection::Up);
            markDirty();
            break;
        case SDLK_DOWN:
            m_keyboardDownHeld = false;
            resetEdgeTurnProgressForDirection(EdgeDirection::Down, true);
            armDoubleTapEdgeTurnDirection(EdgeDirection::Down);
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
            m_dpadRightButtonDown = true;
            if (!m_dpadRightHeld)
            { // Only on true initial press
                m_dpadRightHeld = true;
                handleDpadNudgeRight();
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            m_dpadLeftButtonDown = true;
            if (!m_dpadLeftHeld)
            { // Only on true initial press
                m_dpadLeftHeld = true;
                handleDpadNudgeLeft();
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            m_dpadUpButtonDown = true;
            if (!m_dpadUpHeld)
            { // Only on true initial press
                m_dpadUpHeld = true;
                handleDpadNudgeUp();
            }
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            m_dpadDownButtonDown = true;
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
            m_dpadRightButtonDown = false;
            m_dpadRightHeld = false;
            resetEdgeTurnProgressForDirection(EdgeDirection::Right, true);
            armDoubleTapEdgeTurnDirection(EdgeDirection::Right);
            markDirty();
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            m_dpadLeftButtonDown = false;
            m_dpadLeftHeld = false;
            resetEdgeTurnProgressForDirection(EdgeDirection::Left, true);
            armDoubleTapEdgeTurnDirection(EdgeDirection::Left);
            markDirty();
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            m_dpadUpButtonDown = false;
            m_dpadUpHeld = false;
            resetEdgeTurnProgressForDirection(EdgeDirection::Up, true);
            armDoubleTapEdgeTurnDirection(EdgeDirection::Up);
            markDirty();
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            m_dpadDownButtonDown = false;
            m_dpadDownHeld = false;
            resetEdgeTurnProgressForDirection(EdgeDirection::Down, true);
            armDoubleTapEdgeTurnDirection(EdgeDirection::Down);
            markDirty();
            break;
        }
        break;

    case SDL_CONTROLLERAXISMOTION:
    {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
        {
            m_leftStickX = event.caxis.value;
        }
        else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
        {
            m_leftStickY = event.caxis.value;
        }
        else
        {
            break;
        }

        const bool rightActive = m_leftStickX > PlatformConstants::AXIS_DEAD_ZONE;
        const bool leftActive = m_leftStickX < -PlatformConstants::AXIS_DEAD_ZONE;
        const bool upActive = m_leftStickY < -PlatformConstants::AXIS_DEAD_ZONE;
        const bool downActive = m_leftStickY > PlatformConstants::AXIS_DEAD_ZONE;

        auto handlePress = [&](bool desired, bool& state, const std::function<void()>& nudgeFn)
        {
            if (desired && !state)
            {
                state = true;
                nudgeFn();
            }
        };

        bool releasedAny = false;
        auto handleRelease = [&](bool desired, bool& state, EdgeDirection direction, bool buttonDown)
        {
            // Don't let analog stick clear held state if a D-pad button is physically pressed
            if (buttonDown)
                return;
            if (!desired && state)
            {
                state = false;
                resetEdgeTurnProgressForDirection(direction, true);
                armDoubleTapEdgeTurnDirection(direction);
                releasedAny = true;
            }
        };

        handlePress(rightActive, m_dpadRightHeld, [this]()
                    { handleDpadNudgeRight(); });
        handlePress(leftActive, m_dpadLeftHeld, [this]()
                    { handleDpadNudgeLeft(); });
        handlePress(upActive, m_dpadUpHeld, [this]()
                    { handleDpadNudgeUp(); });
        handlePress(downActive, m_dpadDownHeld, [this]()
                    { handleDpadNudgeDown(); });

        handleRelease(rightActive, m_dpadRightHeld, EdgeDirection::Right, m_dpadRightButtonDown);
        handleRelease(leftActive, m_dpadLeftHeld, EdgeDirection::Left, m_dpadLeftButtonDown);
        handleRelease(upActive, m_dpadUpHeld, EdgeDirection::Up, m_dpadUpButtonDown);
        handleRelease(downActive, m_dpadDownHeld, EdgeDirection::Down, m_dpadDownButtonDown);

        if (releasedAny)
        {
            markDirty();
        }
        break;
    }

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

void App::refreshPageCountFromDocument()
{
    if (!m_document || !m_navigationManager || !m_inputManager)
    {
        return;
    }

    auto* muDoc = dynamic_cast<MuPdfDocument*>(m_document.get());
    if (muDoc && !muDoc->isPageCountFinal())
    {
        return;
    }

    int docPageCount = m_document->getPageCount();
    if (docPageCount <= 0)
    {
        return;
    }

    int currentNavCount = m_navigationManager->getPageCount();
    if (docPageCount == currentNavCount)
    {
        return;
    }

    m_navigationManager->setPageCount(docPageCount);
    m_navigationManager->setDisplayPageCount(docPageCount, false);
    m_inputManager->setPageCount(docPageCount);

    if (m_guiManager)
    {
        m_guiManager->setPageCount(docPageCount, false);
    }

    int currentPage = m_navigationManager->getCurrentPage();

    // If we parked on page 0 after a CSS reopen, restore the desired page
    // now that the real page count is known.
    if (m_pendingPageRestore >= 0)
    {
        currentPage = std::min(m_pendingPageRestore, docPageCount - 1);
        m_pendingPageRestore = -1;
        m_navigationManager->setCurrentPage(currentPage);
    }
    else if (currentPage >= docPageCount)
    {
        currentPage = docPageCount - 1;
        m_navigationManager->setCurrentPage(currentPage);
    }

    if (m_guiManager)
    {
        m_guiManager->setCurrentPage(currentPage);
    }

    markDirty();
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

bool App::saveConfigWithRuntimeOverrides(const FontConfig& config)
{
    FontConfig persistedConfig = config;
    if (m_launchOptions.forceShowImagesInFileBrowser)
    {
        persistedConfig.showImagesInFileBrowser = m_cachedConfig.showImagesInFileBrowser;
    }
    return m_optionsManager->saveConfig(persistedConfig);
}

void App::applyPendingFontChange()
{
    if (!m_pendingFontChange)
    {
        return; // No pending font change
    }

    // Check if font, size, or style actually changed
    bool fontChanged = (m_pendingFontConfig.fontName != m_cachedConfig.fontName);
    bool sizeChanged = (m_pendingFontConfig.fontSize != m_cachedConfig.fontSize);
    bool styleChanged = (m_pendingFontConfig.readingStyle != m_cachedConfig.readingStyle);
    bool zoomStepChanged = (m_pendingFontConfig.zoomStep != m_cachedConfig.zoomStep);
    bool showImagesChanged = (m_pendingFontConfig.showImagesInFileBrowser != m_cachedConfig.showImagesInFileBrowser);
    bool edgeTurnHoldChanged = (m_pendingFontConfig.edgeTurnHoldDurationMs != m_cachedConfig.edgeTurnHoldDurationMs);
    bool edgePageTurnsModeChanged = (m_pendingFontConfig.edgePageTurnsMode != m_cachedConfig.edgePageTurnsMode);
    bool minimapChanged = (m_pendingFontConfig.showDocumentMinimap != m_cachedConfig.showDocumentMinimap);
    bool keepPanningChanged = (m_pendingFontConfig.keepPanningPosition != m_cachedConfig.keepPanningPosition);
    bool pageOverlayChanged = (m_pendingFontConfig.showPageIndicatorOverlay != m_cachedConfig.showPageIndicatorOverlay);
    bool scaleOverlayChanged = (m_pendingFontConfig.showScaleOverlay != m_cachedConfig.showScaleOverlay);

    if (!fontChanged && !sizeChanged && !styleChanged)
    {
        std::cout << "No font/size/style change detected - skipping document reopen" << std::endl;

        // Even if font/size/style didn't change, we still need to save other setting changes
        if (zoomStepChanged || showImagesChanged || edgeTurnHoldChanged || edgePageTurnsModeChanged ||
            minimapChanged || keepPanningChanged || pageOverlayChanged || scaleOverlayChanged)
        {
            std::cout << "Runtime setting changed - saving config" << std::endl;
            saveConfigWithRuntimeOverrides(m_pendingFontConfig);
            refreshCachedConfig(); // Update cache after save

            if (zoomStepChanged)
            {
                m_inputManager->setZoomStep(m_pendingFontConfig.zoomStep);
            }
            if (m_renderManager)
            {
                m_renderManager->setShowMinimap(m_cachedConfig.showDocumentMinimap);
                m_renderManager->setShowPageIndicatorOverlay(m_cachedConfig.showPageIndicatorOverlay);
                m_renderManager->setShowScaleOverlay(m_cachedConfig.showScaleOverlay);
            }
            if (keepPanningChanged && m_navigationManager)
            {
                m_navigationManager->setKeepPanningPosition(m_cachedConfig.keepPanningPosition);
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
        if (auto textDoc = dynamic_cast<TextDocument*>(m_document.get()))
        {
            textDoc->setFontConfig(m_pendingFontConfig);

            int newCount = textDoc->getPageCount();
            m_navigationManager->setPageCount(newCount);
            m_navigationManager->setDisplayPageCount(newCount, false);

            if (m_guiManager)
            {
                m_guiManager->setPageCount(newCount, false);
            }
            if (m_inputManager)
            {
                m_inputManager->setPageCount(newCount);
            }

            int currentPage = m_navigationManager->getCurrentPage();
            if (currentPage >= newCount)
            {
                m_navigationManager->setCurrentPage(std::max(0, newCount - 1));
            }

            saveConfigWithRuntimeOverrides(m_pendingFontConfig);
            refreshCachedConfig();

            uint8_t bgR, bgG, bgB;
            OptionsManager::getReadingStyleBackgroundColor(m_pendingFontConfig.readingStyle, bgR, bgG, bgB);
            m_renderManager->setBackgroundColor(bgR, bgG, bgB);

            markDirty();

            if (m_guiManager && m_guiManager->isFontMenuVisible())
            {
                m_guiManager->toggleFontMenu();
            }

            std::cout << "Applied font configuration to text document: " << m_pendingFontConfig.fontName
                      << " at " << m_pendingFontConfig.fontSize << "pt" << std::endl;
            m_pendingFontChange = false;
            return;
        }

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
                    // After reopening with new CSS the page count is only an
                    // estimate (based on file size).  Navigating to the old page
                    // number can trigger "invalid page number" exceptions if the
                    // new styling produces fewer pages.  Park on page 0 and let
                    // refreshPageCountFromDocument() restore the position once
                    // the async page count finalises.
                    int pageCount = m_document->getPageCount();
                    if (muDoc->isPageCountEstimated())
                    {
                        m_pendingPageRestore = currentPage;
                        m_navigationManager->setCurrentPage(0);
                    }
                    else if (currentPage >= 0 && currentPage < pageCount)
                    {
                        m_navigationManager->setCurrentPage(currentPage);
                    }
                    else
                    {
                        m_navigationManager->setCurrentPage(std::max(0, pageCount - 1));
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

                    // Update page count after reopening - pass the actual
                    // estimated status so navigation isn't prematurely capped
                    // before the async page count thread finishes.
                    bool isEstimated = muDoc->isPageCountEstimated();
                    m_navigationManager->setPageCount(pageCount);
                    m_navigationManager->setDisplayPageCount(pageCount, isEstimated);

                    // Update GUI manager's page count for the font menu display
                    if (m_guiManager)
                    {
                        m_guiManager->setPageCount(pageCount, isEstimated);
                    }

                    // Clamp scroll to ensure it's within bounds
                    m_viewportManager->clampScroll();

                    // Save the configuration
                    saveConfigWithRuntimeOverrides(m_pendingFontConfig);

                    // Refresh cached config after saving
                    refreshCachedConfig();

                    if (m_renderManager)
                    {
                        m_renderManager->setShowMinimap(m_cachedConfig.showDocumentMinimap);
                        m_renderManager->setShowPageIndicatorOverlay(m_cachedConfig.showPageIndicatorOverlay);
                        m_renderManager->setShowScaleOverlay(m_cachedConfig.showScaleOverlay);
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

float& App::edgeTurnHoldForDirection(EdgeDirection direction)
{
    switch (direction)
    {
    case EdgeDirection::Right:
        return m_edgeTurnHoldRight;
    case EdgeDirection::Left:
        return m_edgeTurnHoldLeft;
    case EdgeDirection::Up:
        return m_edgeTurnHoldUp;
    case EdgeDirection::Down:
        return m_edgeTurnHoldDown;
    case EdgeDirection::None:
        break;
    }

    throw std::logic_error("Invalid edge-turn direction");
}

const float& App::edgeTurnHoldForDirection(EdgeDirection direction) const
{
    switch (direction)
    {
    case EdgeDirection::Right:
        return m_edgeTurnHoldRight;
    case EdgeDirection::Left:
        return m_edgeTurnHoldLeft;
    case EdgeDirection::Up:
        return m_edgeTurnHoldUp;
    case EdgeDirection::Down:
        return m_edgeTurnHoldDown;
    case EdgeDirection::None:
        break;
    }

    throw std::logic_error("Invalid edge-turn direction");
}

float& App::edgeTurnCooldownForDirection(EdgeDirection direction)
{
    switch (direction)
    {
    case EdgeDirection::Right:
        return m_edgeTurnCooldownRight;
    case EdgeDirection::Left:
        return m_edgeTurnCooldownLeft;
    case EdgeDirection::Up:
        return m_edgeTurnCooldownUp;
    case EdgeDirection::Down:
        return m_edgeTurnCooldownDown;
    case EdgeDirection::None:
        break;
    }

    throw std::logic_error("Invalid edge-turn direction");
}

void App::resetEdgeTurnHolds()
{
    m_edgeTurnHoldRight = 0.0f;
    m_edgeTurnHoldLeft = 0.0f;
    m_edgeTurnHoldUp = 0.0f;
    m_edgeTurnHoldDown = 0.0f;
}

void App::resetEdgeTurnProgressForDirection(EdgeDirection direction, bool startCooldown)
{
    if (direction == EdgeDirection::None)
    {
        return;
    }

    float& hold = edgeTurnHoldForDirection(direction);
    if (startCooldown && hold > 0.0f)
    {
        edgeTurnCooldownForDirection(direction) = SDL_GetTicks() / 1000.0f;
    }
    hold = 0.0f;
}

bool App::canTurnPageInDirection(EdgeDirection direction) const
{
    if (!m_navigationManager)
    {
        return false;
    }

    const int currentPage = m_navigationManager->getCurrentPage();
    const int pageCount = m_navigationManager->getPageCount();

    switch (direction)
    {
    case EdgeDirection::Right:
    case EdgeDirection::Down:
        return currentPage < pageCount - 1;
    case EdgeDirection::Left:
    case EdgeDirection::Up:
        return currentPage > 0;
    case EdgeDirection::None:
        return false;
    }

    return false;
}

bool App::isDirectionAtTurnEdge(EdgeDirection direction) const
{
    if (!m_viewportManager)
    {
        return false;
    }

    constexpr int kEdgeTolerance = 2;
    const int maxX = m_viewportManager->getMaxScrollX();
    const int maxY = m_viewportManager->getMaxScrollY();

    switch (direction)
    {
    case EdgeDirection::Right:
        return maxX == 0 || m_viewportManager->getScrollX() <= (-maxX + kEdgeTolerance);
    case EdgeDirection::Left:
        return maxX == 0 || m_viewportManager->getScrollX() >= (maxX - kEdgeTolerance);
    case EdgeDirection::Down:
        return maxY == 0 || m_viewportManager->getScrollY() <= (-maxY + kEdgeTolerance);
    case EdgeDirection::Up:
        return maxY == 0 || m_viewportManager->getScrollY() >= (maxY - kEdgeTolerance);
    case EdgeDirection::None:
        return false;
    }

    return false;
}

bool App::isEligibleEdgeTurnDirection(EdgeDirection direction) const
{
    return canTurnPageInDirection(direction) && isDirectionAtTurnEdge(direction);
}

void App::armDoubleTapEdgeTurnDirection(EdgeDirection direction)
{
    if (m_cachedConfig.edgePageTurnsMode != EdgePageTurnsMode::DoubleTap ||
        direction == EdgeDirection::None)
    {
        return;
    }

    const float currentTime = SDL_GetTicks() / 1000.0f;
    const float directionCooldown = edgeTurnCooldownForDirection(direction);
    const bool inCooldown = directionCooldown > 0.0f &&
                            (currentTime - directionCooldown < m_edgeTurnCooldownDuration);

    if (inCooldown)
    {
        clearDoubleTapEdgeTurnStateIfInvalid();
        return;
    }

    if (isEligibleEdgeTurnDirection(direction))
    {
        m_doubleTapArmedDirection = direction;
    }
    else if (m_doubleTapArmedDirection == direction)
    {
        clearDoubleTapEdgeTurnState();
    }
}

void App::clearDoubleTapEdgeTurnState()
{
    m_doubleTapArmedDirection = EdgeDirection::None;
}

void App::clearDoubleTapEdgeTurnStateIfInvalid()
{
    if (m_doubleTapArmedDirection != EdgeDirection::None &&
        !isEligibleEdgeTurnDirection(m_doubleTapArmedDirection))
    {
        clearDoubleTapEdgeTurnState();
    }
}

bool App::performEdgeTurn(EdgeDirection direction)
{
    if (direction == EdgeDirection::None ||
        !m_document || !m_navigationManager || !m_viewportManager ||
        !canTurnPageInDirection(direction) || m_navigationManager->isInPageChangeCooldown())
    {
        return false;
    }

    float currentTime = SDL_GetTicks() / 1000.0f;
    float& directionCooldown = edgeTurnCooldownForDirection(direction);
    const bool inCooldown = directionCooldown > 0.0f &&
                            (currentTime - directionCooldown < m_edgeTurnCooldownDuration);
    if (inCooldown)
    {
        return false;
    }

    const bool moveToNextPage = direction == EdgeDirection::Right || direction == EdgeDirection::Down;
    if (moveToNextPage)
    {
        m_navigationManager->goToNextPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                          { markDirty(); }, [this]()
                                          { updateScaleDisplayTime(); }, [this]()
                                          {
                                              updatePageDisplayTime();
                                              m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
    }
    else
    {
        m_navigationManager->goToPreviousPage(m_document.get(), m_viewportManager.get(), makeSetCurrentPageCallback(), [this]()
                                              { markDirty(); }, [this]()
                                              { updateScaleDisplayTime(); }, [this]()
                                              {
                                                  updatePageDisplayTime();
                                                  m_readingHistoryManager->updateLastPage(m_documentPath, m_navigationManager->getCurrentPage()); });
    }

    switch (direction)
    {
    case EdgeDirection::Right:
        m_viewportManager->setScrollX(m_viewportManager->getMaxScrollX());
        break;
    case EdgeDirection::Left:
        m_viewportManager->setScrollX(-m_viewportManager->getMaxScrollX());
        break;
    case EdgeDirection::Down:
        m_viewportManager->setScrollY(m_viewportManager->getMaxScrollY());
        break;
    case EdgeDirection::Up:
        m_viewportManager->setScrollY(-m_viewportManager->getMaxScrollY());
        break;
    case EdgeDirection::None:
        break;
    }

    m_viewportManager->clampScroll();
    directionCooldown = currentTime;
    resetEdgeTurnHolds();
    clearDoubleTapEdgeTurnState();
    return true;
}

bool App::handleDoubleTapEdgePress(EdgeDirection direction)
{
    if (m_cachedConfig.edgePageTurnsMode != EdgePageTurnsMode::DoubleTap ||
        direction == EdgeDirection::None)
    {
        return false;
    }

    if (m_doubleTapArmedDirection != EdgeDirection::None &&
        m_doubleTapArmedDirection != direction)
    {
        clearDoubleTapEdgeTurnState();
    }

    if (!isEligibleEdgeTurnDirection(direction))
    {
        if (m_doubleTapArmedDirection == direction)
        {
            clearDoubleTapEdgeTurnState();
        }
        return false;
    }

    if (m_doubleTapArmedDirection == direction)
    {
        clearDoubleTapEdgeTurnState();
        return performEdgeTurn(direction);
    }

    return false;
}

bool App::updateHeldPanning(float dt)
{
    bool changed = false;
    const EdgePageTurnsMode edgePageTurnsMode = m_cachedConfig.edgePageTurnsMode;
    const bool automaticEdgeTurns = edgePageTurnsMode == EdgePageTurnsMode::Automatic;
    const bool doubleTapEdgeTurns = edgePageTurnsMode == EdgePageTurnsMode::DoubleTap;
    const float configuredEdgeTurnThreshold = static_cast<float>(std::max(0, m_cachedConfig.edgeTurnHoldDurationMs)) / 1000.0f;
    const bool instantPageTurns = automaticEdgeTurns && configuredEdgeTurnThreshold <= 0.0f;
    const float effectiveEdgeTurnThreshold = instantPageTurns ? 0.001f : configuredEdgeTurnThreshold;

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

    if (!automaticEdgeTurns)
    {
        resetEdgeTurnHolds();
        if (doubleTapEdgeTurns)
        {
            clearDoubleTapEdgeTurnStateIfInvalid();
        }
        else
        {
            clearDoubleTapEdgeTurnState();
        }

        if (m_edgeTurnHoldRight != oldEdgeTurnHoldRight ||
            m_edgeTurnHoldLeft != oldEdgeTurnHoldLeft ||
            m_edgeTurnHoldUp != oldEdgeTurnHoldUp ||
            m_edgeTurnHoldDown != oldEdgeTurnHoldDown)
        {
            markDirty();
        }

        return changed;
    }

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
            resetEdgeTurnHolds();
        }
    }
    else if (scrollingOccurred)
    {
        // Reset edge-turn timers if user is actively scrolling - only start timer when stationary at edge
        resetEdgeTurnHolds();
    }
    else
    {
        // Only accumulate edge-turn time when not in scroll timeout AND not actively scrolling
        if (maxX == 0)
        {
            if (m_dpadRightHeld || m_keyboardRightHeld)
            {
                if (m_edgeTurnFiredRight)
                {
                    // Already fired during this hold at auto-zoom; suppress re-accumulation
                }
                else if (instantPageTurns && m_edgeTurnHoldRight == 0.0f)
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
                m_edgeTurnFiredRight = false;
            }
            if (m_dpadLeftHeld || m_keyboardLeftHeld)
            {
                if (m_edgeTurnFiredLeft)
                {
                    // Already fired during this hold at auto-zoom; suppress re-accumulation
                }
                else if (instantPageTurns && m_edgeTurnHoldLeft == 0.0f)
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
                m_edgeTurnFiredLeft = false;
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
                m_edgeTurnHoldLeft = 0.0f;
            }
        }
    }

    if (m_edgeTurnHoldRight >= effectiveEdgeTurnThreshold)
    {
        changed = performEdgeTurn(EdgeDirection::Right) || changed;
        m_edgeTurnHoldRight = 0.0f;
        if (maxX == 0)
            m_edgeTurnFiredRight = true;
    }
    else if (m_edgeTurnHoldLeft >= effectiveEdgeTurnThreshold)
    {
        changed = performEdgeTurn(EdgeDirection::Left) || changed;
        m_edgeTurnHoldLeft = 0.0f;
        if (maxX == 0)
            m_edgeTurnFiredLeft = true;
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
                if (m_edgeTurnFiredDown)
                {
                    // Already fired during this hold at auto-zoom; suppress re-accumulation
                }
                else if (instantPageTurns && m_edgeTurnHoldDown == 0.0f)
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
                m_edgeTurnFiredDown = false;
            }
            if (m_dpadUpHeld || m_keyboardUpHeld)
            {
                if (m_edgeTurnFiredUp)
                {
                    // Already fired during this hold at auto-zoom; suppress re-accumulation
                }
                else if (instantPageTurns && m_edgeTurnHoldUp == 0.0f)
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
                m_edgeTurnFiredUp = false;
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
        m_edgeTurnHoldUp = 0.0f;
        m_edgeTurnHoldDown = 0.0f;
    }

    if (m_edgeTurnHoldDown >= effectiveEdgeTurnThreshold)
    {
        changed = performEdgeTurn(EdgeDirection::Down) || changed;
        m_edgeTurnHoldDown = 0.0f;
        if (maxY == 0)
            m_edgeTurnFiredDown = true;
    }
    else if (m_edgeTurnHoldUp >= effectiveEdgeTurnThreshold)
    {
        changed = performEdgeTurn(EdgeDirection::Up) || changed;
        m_edgeTurnHoldUp = 0.0f;
        if (maxY == 0)
            m_edgeTurnFiredUp = true;
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
    const bool automaticEdgeTurns = m_cachedConfig.edgePageTurnsMode == EdgePageTurnsMode::Automatic;
    const bool instantEdgeTurns = automaticEdgeTurns && m_cachedConfig.edgeTurnHoldDurationMs <= 0;

    if (handleDoubleTapEdgePress(EdgeDirection::Right))
    {
        return;
    }

    // Right nudge while already at right edge
    if (isDirectionAtTurnEdge(EdgeDirection::Right))
    {
        if (!automaticEdgeTurns)
        {
            return;
        }
        if (maxX == 0 && instantEdgeTurns)
        {
            // Page fits horizontally (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldRight == 0.0f) // Only if no progress bar is currently running
            {
                performEdgeTurn(EdgeDirection::Right);
            }
        }
        else
        {
            // For zoomed pages (maxX > 0): defer to progress bar system
        }
        return;
    }
    m_viewportManager->setScrollX(m_viewportManager->getScrollX() - 50);
    m_viewportManager->clampScroll();
    clearDoubleTapEdgeTurnStateIfInvalid();
}

void App::handleDpadNudgeLeft()
{
    const int maxX = m_viewportManager->getMaxScrollX();
    const bool automaticEdgeTurns = m_cachedConfig.edgePageTurnsMode == EdgePageTurnsMode::Automatic;
    const bool instantEdgeTurns = automaticEdgeTurns && m_cachedConfig.edgeTurnHoldDurationMs <= 0;

    if (handleDoubleTapEdgePress(EdgeDirection::Left))
    {
        return;
    }

    // Left nudge while already at left edge
    if (isDirectionAtTurnEdge(EdgeDirection::Left))
    {
        if (!automaticEdgeTurns)
        {
            return;
        }
        if (maxX == 0 && instantEdgeTurns)
        {
            // Page fits horizontally (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldLeft == 0.0f) // Only if no progress bar is currently running
            {
                performEdgeTurn(EdgeDirection::Left);
            }
        }
        // For zoomed pages (maxX > 0): always defer to progress bar system
        // This ensures a progress bar always appears when holding D-pad at edge
        return;
    }
    m_viewportManager->setScrollX(m_viewportManager->getScrollX() + 50);
    m_viewportManager->clampScroll();
    clearDoubleTapEdgeTurnStateIfInvalid();
}

void App::handleDpadNudgeDown()
{
    const int maxY = m_viewportManager->getMaxScrollY();
    const bool automaticEdgeTurns = m_cachedConfig.edgePageTurnsMode == EdgePageTurnsMode::Automatic;
    const bool instantEdgeTurns = automaticEdgeTurns && m_cachedConfig.edgeTurnHoldDurationMs <= 0;

    if (handleDoubleTapEdgePress(EdgeDirection::Down))
    {
        return;
    }

    // Down nudge while already at bottom edge
    if (isDirectionAtTurnEdge(EdgeDirection::Down))
    {
        if (!automaticEdgeTurns)
        {
            return;
        }
        if (maxY == 0 && instantEdgeTurns)
        {
            // Page fits vertically (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldDown == 0.0f) // Only if no progress bar is currently running
            {
                performEdgeTurn(EdgeDirection::Down);
            }
        }
        // For zoomed pages (maxY > 0): always defer to progress bar system
        // This ensures a progress bar always appears when holding D-pad at edge
        return;
    }
    m_viewportManager->setScrollY(m_viewportManager->getScrollY() - 50);
    m_viewportManager->clampScroll();
    clearDoubleTapEdgeTurnStateIfInvalid();
}

void App::handleDpadNudgeUp()
{
    const int maxY = m_viewportManager->getMaxScrollY();
    const bool automaticEdgeTurns = m_cachedConfig.edgePageTurnsMode == EdgePageTurnsMode::Automatic;
    const bool instantEdgeTurns = automaticEdgeTurns && m_cachedConfig.edgeTurnHoldDurationMs <= 0;

    if (handleDoubleTapEdgePress(EdgeDirection::Up))
    {
        return;
    }

    // Up nudge while already at top edge
    if (isDirectionAtTurnEdge(EdgeDirection::Up))
    {
        if (!automaticEdgeTurns)
        {
            return;
        }
        if (maxY == 0 && instantEdgeTurns)
        {
            // Page fits vertically (fit-to-width): allow immediate page change via nudge
            // The progress bar system will also work in parallel for sustained holds
            if (m_edgeTurnHoldUp == 0.0f) // Only if no progress bar is currently running
            {
                performEdgeTurn(EdgeDirection::Up);
            }
        }
        // For zoomed pages (maxY > 0): always defer to progress bar system
        // This ensures a progress bar always appears when holding D-pad at edge
        return;
    }
    m_viewportManager->setScrollY(m_viewportManager->getScrollY() + 50);
    m_viewportManager->clampScroll();
    clearDoubleTapEdgeTurnStateIfInvalid();
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
        resetEdgeTurnHolds();
        clearDoubleTapEdgeTurnState();
        if (m_guiManager)
        {
            m_guiManager->setCurrentPage(page);
        }
    };
}
