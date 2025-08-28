#include "app.h"
#include "renderer.h"
#include "text_renderer.h"
#include "document.h"
#include "pdf_document.h"
#include "power_handler.h"

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
      m_gameController(nullptr), m_gameControllerInstanceID(-1)
{

    // Pass the pre-initialized window and renderer to the Renderer object
    m_renderer = std::make_unique<Renderer>(window, renderer);

    m_textRenderer = std::make_unique<TextRenderer>(m_renderer->getSDLRenderer(), "res/Roboto-Regular.ttf", 16);

    // Initialize power handler
    m_powerHandler = std::make_unique<PowerHandler>();

    if (filename.size() >= 4 &&
        std::equal(filename.end() - 4, filename.end(),
                   ".pdf",
                   [](unsigned char a, unsigned char b)
                   {
                       return std::tolower(a) == std::tolower(b);
                   }))
    {
        m_document = std::make_unique<PdfDocument>();
    }
    else
    {
        throw std::runtime_error("Unsupported file format: " + filename);
    }

    if (!m_document->open(filename))
    {
        throw std::runtime_error("Failed to open document: " + filename);
    }

    m_pageCount = m_document->getPageCount();
    if (m_pageCount == 0)
    {
        throw std::runtime_error("Document contains no pages: " + filename);
    }

    // Initial page load and fit
    loadDocument();

    // Initialize game controllers
    initializeGameControllers();
}

App::~App()
{
    if (m_powerHandler) {
        m_powerHandler->stop();
    }
    closeGameControllers();
}

void App::run()
{
    m_prevTick = SDL_GetTicks();

    // Start power button monitoring
    if (!m_powerHandler->start()) {
        std::cerr << "Warning: Failed to start power button monitoring" << std::endl;
    }

    SDL_Event event;
    while (m_running)
    {
        while (SDL_PollEvent(&event) != 0)
        {
            handleEvent(event);
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - m_prevTick) / 1000.0f;
        m_prevTick = now;

        updateHeldPanning(dt);

        renderCurrentPage();
        renderUI();
        m_renderer->present();
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
        if (event.wheel.y > 0)
        {
            if (SDL_GetModState() & KMOD_CTRL)
            {
                zoom(10);
            }
            else
            {
                m_scrollY += 50;
            }
        }
        else if (event.wheel.y < 0)
        {
            if (SDL_GetModState() & KMOD_CTRL)
            {
                zoom(-10);
            }
            else
            {
                m_scrollY -= 50;
            }
        }
        clampScroll();
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
        if (m_isDragging)
        {
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
        if (event.caxis.which == m_gameControllerInstanceID)
        {
            const Sint16 AXIS_DEAD_ZONE = 8000;
            // --- L2 / R2 as analog axes: jump ±10 pages on a strong press ---
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT &&
                event.caxis.value > AXIS_DEAD_ZONE)
            {
                jumpPages(-10);
            }
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT &&
                event.caxis.value > AXIS_DEAD_ZONE)
            {
                jumpPages(+10);
            }

            switch (event.caxis.axis)
            {
            case SDL_CONTROLLER_AXIS_LEFTX:
            case SDL_CONTROLLER_AXIS_RIGHTX:
                if (event.caxis.value < -AXIS_DEAD_ZONE)
                {
                    m_scrollX += 20;
                }
                else if (event.caxis.value > AXIS_DEAD_ZONE)
                {
                    m_scrollX -= 20;
                }
                break;
            case SDL_CONTROLLER_AXIS_LEFTY:
            case SDL_CONTROLLER_AXIS_RIGHTY:
                if (event.caxis.value < -AXIS_DEAD_ZONE)
                {
                    m_scrollY += 20;
                }
                else if (event.caxis.value > AXIS_DEAD_ZONE)
                {
                    m_scrollY -= 20;
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
                goToPreviousPage();
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                goToNextPage();
                break;

            // --- Y / B: zoom in/out ---
            case SDL_CONTROLLER_BUTTON_Y:
                zoom(10);
                break;
            case SDL_CONTROLLER_BUTTON_B:
                zoom(-10);
                break;

            // --- X: Best fit width (stub -> fits width) ---
            case SDL_CONTROLLER_BUTTON_X:
                fitPageToWidth();
                break;

            // --- A: Rotate (stub) ---
            case SDL_CONTROLLER_BUTTON_A:
                rotateClockwise();
                break;

            // --- MENU/START: Quit ---
            case SDL_CONTROLLER_BUTTON_GUIDE:
                action = AppAction::Quit;
                break; // MENU on Brick
            case SDL_CONTROLLER_BUTTON_START:
                toggleMirrorHorizontal();
                break;

            // --- SELECT/START for mirroring (stubs) ---
            case SDL_CONTROLLER_BUTTON_BACK:
                toggleMirrorVertical();
                break; // SELECT
            // note: START already used for Quit above; change if you prefer mirroring there
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
    }
}

void App::loadDocument()
{
    m_currentPage = 0;
    fitPageToWindow();
}

void App::renderCurrentPage()
{
    m_renderer->clear(255, 255, 255, 255);

    int winW = m_renderer->getWindowWidth();
    int winH = m_renderer->getWindowHeight();

    int srcW, srcH;
    std::vector<uint8_t> pixelData = m_document->renderPage(m_currentPage, srcW, srcH, m_currentScale);

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

void App::renderUI()
{
    int baseFontSize = 16;
    m_textRenderer->setFontSize(baseFontSize);

    SDL_Color textColor = {0, 0, 0, 255};
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

void App::goToNextPage()
{
    if (m_currentPage < m_pageCount - 1)
    {
        m_currentPage++;
        onPageChangedKeepZoom();
        alignToTopOfCurrentPage();
    }
}

void App::goToPreviousPage()
{
    if (m_currentPage > 0)
    {
        m_currentPage--;
        onPageChangedKeepZoom();
        alignToTopOfCurrentPage();
    }
}

void App::goToPage(int pageNum)
{
    if (pageNum >= 0 && pageNum < m_pageCount)
    {
        m_currentPage = pageNum;
        onPageChangedKeepZoom();
        alignToTopOfCurrentPage();
    }
}

void App::zoom(int delta)
{
    int oldScale = m_currentScale;
    m_currentScale += delta;
    if (m_currentScale < 10)
        m_currentScale = 10;
    if (m_currentScale > 500)
        m_currentScale = 500;

    recenterScrollOnZoom(oldScale, m_currentScale);
    clampScroll();
}

void App::zoomTo(int scale)
{
    int oldScale = m_currentScale;
    m_currentScale = scale;
    if (m_currentScale < 10)
        m_currentScale = 10;
    if (m_currentScale > 500)
        m_currentScale = 500;

    recenterScrollOnZoom(oldScale, m_currentScale);
    clampScroll();
}

void App::fitPageToWindow()
{
    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

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
    if (m_currentScale > 500)
        m_currentScale = 500;

    m_pageWidth = static_cast<int>(nativeWidth * (m_currentScale / 100.0));
    m_pageHeight = static_cast<int>(nativeHeight * (m_currentScale / 100.0));

    m_scrollX = 0;
    m_scrollY = 0;
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

// ---- helpers (stubs where noted) ----
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
}

void App::toggleMirrorVertical()
{
    m_mirrorV = !m_mirrorV;
}

void App::toggleMirrorHorizontal()
{
    m_mirrorH = !m_mirrorH;
}

void App::fitPageToWidth()
{
    // Approximate: compute scale so page width == window width (height may overflow).
    // You already have 'fitPageToWindow()' for full-page; width-fit is complementary.
    // Implement by measuring page width at 100% and window width, then set m_currentScale.
    // For now, call fitPageToWindow() as a placeholder:
    fitPageToWindow(); // TODO: replace with width-only fit.
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

void App::updateHeldPanning(float dt)
{
    float dx = 0.0f, dy = 0.0f;

    if (m_dpadLeftHeld)
        dx += 1.0f;
    if (m_dpadRightHeld)
        dx -= 1.0f;
    if (m_dpadUpHeld)
        dy += 1.0f;
    if (m_dpadDownHeld)
        dy -= 1.0f;

    if (dx != 0.0f || dy != 0.0f)
    {
        float len = std::sqrt(dx * dx + dy * dy);
        dx /= len;
        dy /= len;

        m_scrollX += static_cast<int>(dx * m_dpadPanSpeed * dt);
        m_scrollY += static_cast<int>(dy * m_dpadPanSpeed * dt);
        clampScroll();
    }

    // --- HORIZONTAL edge → page turn ---
    const int maxX = getMaxScrollX();

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

    if (m_edgeTurnHoldRight >= m_edgeTurnThreshold)
    {
        if (m_currentPage < m_pageCount - 1)
        {
            goToNextPage();
            m_scrollX = getMaxScrollX(); // appear at left edge
            clampScroll();
        }
        m_edgeTurnHoldRight = 0.0f;
    }
    else if (m_edgeTurnHoldLeft >= m_edgeTurnThreshold)
    {
        if (m_currentPage > 0)
        {
            goToPreviousPage();
            m_scrollX = -getMaxScrollX(); // appear at right edge
            clampScroll();
        }
        m_edgeTurnHoldLeft = 0.0f;
    }

    // --- VERTICAL edge → page turn (NEW) ---
    const int maxY = getMaxScrollY();

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

    if (m_edgeTurnHoldDown >= m_edgeTurnThreshold)
    {
        if (m_currentPage < m_pageCount - 1)
        {
            goToNextPage();
            // Land at the top edge of the new page so motion feels continuous downward
            m_scrollY = getMaxScrollY();
            clampScroll();
        }
        m_edgeTurnHoldDown = 0.0f;
    }
    else if (m_edgeTurnHoldUp >= m_edgeTurnThreshold)
    {
        if (m_currentPage > 0)
        {
            goToPreviousPage();
            // Land at the bottom edge of the previous page
            m_scrollY = -getMaxScrollY();
            clampScroll();
        }
        m_edgeTurnHoldUp = 0.0f;
    }
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
        if (m_currentPage < m_pageCount - 1)
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
        if (m_currentPage > 0)
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
        if (m_currentPage < m_pageCount - 1)
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
        if (m_currentPage > 0)
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
    int nativeW = m_document->getPageWidthNative(m_currentPage);
    int nativeH = m_document->getPageHeightNative(m_currentPage);
    if (nativeW <= 0 || nativeH <= 0)
        return;

    m_pageWidth = static_cast<int>(nativeW * (m_currentScale / 100.0));
    m_pageHeight = static_cast<int>(nativeH * (m_currentScale / 100.0));

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
