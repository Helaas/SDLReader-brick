#include "app.h"
#include "render_manager.h"
#include "document.h"
#include "mupdf_document.h"
#include "navigation_manager.h"
#include "renderer.h"
#include "text_renderer.h"
#include "viewport_manager.h"
#include <algorithm>
#include <cmath>
#include <iostream>

RenderManager::RenderManager(SDL_Window* window, SDL_Renderer* renderer)
{
    // Initialize renderer with the pre-initialized SDL objects
    m_renderer = std::make_unique<Renderer>(window, renderer);

    // Initialize text renderer with default font
    m_textRenderer = std::make_unique<TextRenderer>(m_renderer->getSDLRenderer(), "fonts/Roboto-Regular.ttf", 16);

    // Initialize display timers
    m_state.scaleDisplayTime = SDL_GetTicks();
    m_state.pageDisplayTime = SDL_GetTicks();
}

bool RenderManager::initialize()
{
    // Additional initialization if needed
    return m_renderer && m_textRenderer;
}

void RenderManager::renderCurrentPage(Document* document, NavigationManager* navigationManager,
                                      ViewportManager* viewportManager, std::mutex& documentMutex,
                                      bool isDragging)
{
    Uint32 renderStart = SDL_GetTicks();

    m_renderer->clear(255, 255, 255, 255);

    int winW = m_renderer->getWindowWidth();
    int winH = m_renderer->getWindowHeight();

    int srcW, srcH;
    std::vector<uint32_t> argbData;
    {
        // Lock the document mutex to ensure thread-safe access
        std::lock_guard<std::mutex> lock(documentMutex);

        // Try to use ARGB rendering for better performance
        auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
        if (muPdfDoc)
        {
            try
            {
                argbData = muPdfDoc->renderPageARGB(navigationManager->getCurrentPage(), srcW, srcH, viewportManager->getCurrentScale());
            }
            catch (const std::exception& e)
            {
                // Fallback to RGB rendering
                std::vector<uint8_t> rgbData = document->renderPage(navigationManager->getCurrentPage(), srcW, srcH, viewportManager->getCurrentScale());
                // Convert to ARGB manually
                argbData.resize(srcW * srcH);
                for (int i = 0; i < srcW * srcH; ++i)
                {
                    argbData[i] = rgb24_to_argb32(rgbData[i * 3], rgbData[i * 3 + 1], rgbData[i * 3 + 2]);
                }
            }
        }
        else
        {
            // Fallback to RGB rendering for other document types
            std::vector<uint8_t> rgbData = document->renderPage(navigationManager->getCurrentPage(), srcW, srcH, viewportManager->getCurrentScale());
            // Convert to ARGB manually
            argbData.resize(srcW * srcH);
            for (int i = 0; i < srcW * srcH; ++i)
            {
                argbData[i] = rgb24_to_argb32(rgbData[i * 3], rgbData[i * 3 + 1], rgbData[i * 3 + 2]);
            }
        }
    }

    // Ensure page dimensions are updated BEFORE calculating positions
    // This prevents warping when switching between pages with different aspect ratios
    int newPageWidth, newPageHeight;

    // displayed page size after rotation
    if (viewportManager->getRotation() % 180 == 0)
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
    if (viewportManager->getPageWidth() != newPageWidth || viewportManager->getPageHeight() != newPageHeight)
    {
        viewportManager->setPageDimensions(newPageWidth, newPageHeight);
        // Clamp scroll position when page dimensions change to prevent out-of-bounds rendering
        viewportManager->clampScroll();
    }

    int posX = (winW - viewportManager->getPageWidth()) / 2 + viewportManager->getScrollX();

    int posY;
    if (viewportManager->getPageHeight() <= winH)
    {
        const auto& state = viewportManager->getState();
        if (state.topAlignWhenFits || state.forceTopAlignNextRender)
            posY = 0;
        else
            posY = (winH - viewportManager->getPageHeight()) / 2;
    }
    else
    {
        posY = (winH - viewportManager->getPageHeight()) / 2 + viewportManager->getScrollY();
    }
    viewportManager->setForceTopAlignNextRender(false);

    m_renderer->renderPageExARGB(argbData, srcW, srcH,
                                 posX, posY, viewportManager->getPageWidth(), viewportManager->getPageHeight(),
                                 static_cast<double>(viewportManager->getRotation()),
                                 viewportManager->currentFlipFlags());

    // Trigger prerendering of adjacent pages for faster page changes
    // Do this after the main render to avoid blocking the current page display
    // Only prerender if zoom is stable (not debouncing), not actively panning, and not in cooldown
    static Uint32 lastPrerenderTrigger = 0;
    Uint32 currentTime = SDL_GetTicks();
    bool prerenderCooldownActive = (currentTime - lastPrerenderTrigger) < 200; // 200ms cooldown

    if (!viewportManager->isZoomDebouncing() && !isDragging && !prerenderCooldownActive)
    {
        auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
        if (muPdfDoc && !muPdfDoc->isPrerenderingActive())
        {
            muPdfDoc->prerenderAdjacentPagesAsync(navigationManager->getCurrentPage(), viewportManager->getCurrentScale());
            lastPrerenderTrigger = currentTime;
        }
    }

    // Measure total render time for dynamic timeout
    m_state.lastRenderDuration = SDL_GetTicks() - renderStart;
    navigationManager->setLastRenderDuration(m_state.lastRenderDuration);
}

void RenderManager::renderUI(App* app, NavigationManager* navigationManager, ViewportManager* viewportManager)
{
    int windowWidth = m_renderer->getWindowWidth();
    int windowHeight = m_renderer->getWindowHeight();

    // Render all UI components
    renderPageInfo(navigationManager, windowWidth, windowHeight);
    renderScaleInfo(viewportManager, windowWidth, windowHeight);
    renderZoomProcessingIndicator(viewportManager, windowWidth, windowHeight);
    renderErrorMessage(windowWidth, windowHeight);
    renderPageJumpInput(navigationManager, windowWidth, windowHeight);
    
    // Render edge turn progress indicators
    if (app)
    {
        renderEdgeTurnProgressIndicator(app, navigationManager, viewportManager, windowWidth, windowHeight);
    }
}

void RenderManager::renderFakeSleepScreen()
{
    // Render black screen for fake sleep mode
    m_renderer->clear(0, 0, 0, 255);
    clearDirtyFlag();
}

void RenderManager::renderPageInfo(NavigationManager* navigationManager, int windowWidth, int windowHeight)
{
    // Only show page info for 2 seconds after it changes
    if ((SDL_GetTicks() - m_state.pageDisplayTime) < RenderState::PAGE_DISPLAY_DURATION)
    {
        m_textRenderer->setFontSize(100); // 100% = normal base size

        SDL_Color textColor = {0, 0, 0, 255};
        std::string pageInfo = "Page: " + std::to_string(navigationManager->getCurrentPage() + 1) + "/" + std::to_string(navigationManager->getPageCount());

        m_textRenderer->renderText(pageInfo,
                                   (windowWidth - static_cast<int>(pageInfo.length()) * 8) / 2,
                                   windowHeight - 30, textColor);
    }
}

void RenderManager::renderScaleInfo(ViewportManager* viewportManager, int windowWidth, int windowHeight)
{
    (void) windowHeight; // Suppress unused parameter warning

    // Only show scale info for 2 seconds after it changes
    if ((SDL_GetTicks() - m_state.scaleDisplayTime) < RenderState::SCALE_DISPLAY_DURATION)
    {
        m_textRenderer->setFontSize(100); // 100% = normal base size

        SDL_Color textColor = {0, 0, 0, 255};
        std::string scaleInfo = "Scale: " + std::to_string(viewportManager->getCurrentScale()) + "%";

        m_textRenderer->renderText(scaleInfo,
                                   windowWidth - static_cast<int>(scaleInfo.length()) * 8 - 10,
                                   10, textColor);
    }
}

void RenderManager::renderZoomProcessingIndicator(ViewportManager* viewportManager, int windowWidth, int windowHeight)
{
    (void) windowHeight; // Suppress unused parameter warning

    if (viewportManager->shouldShowZoomProcessingIndicator())
    {
        SDL_Color processingColor = {255, 255, 0, 255}; // Bright yellow text for high visibility
        SDL_Color processingBgColor = {0, 0, 0, 250};   // Nearly opaque background

        std::string processingText = "Processing zoom...";

        // Use larger font for better visibility during longer operations
        static constexpr int PROCESSING_FONT_SIZE = 150;
        static constexpr int PROCESSING_AVG_CHAR_WIDTH = 12;
        static constexpr int PROCESSING_TEXT_HEIGHT = 24;

        m_textRenderer->setFontSize(PROCESSING_FONT_SIZE);
        int avgCharWidth = PROCESSING_AVG_CHAR_WIDTH;
        int textWidth = static_cast<int>(processingText.length()) * avgCharWidth;
        int textHeight = PROCESSING_TEXT_HEIGHT;

        // Position prominently in top-center
        int textX = (windowWidth - textWidth) / 2;
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
}

void RenderManager::renderErrorMessage(int windowWidth, int windowHeight)
{
    if (!m_state.errorMessage.empty() && (SDL_GetTicks() - m_state.errorMessageTime) < RenderState::ERROR_MESSAGE_DURATION)
    {
        int baseFontSize = 16;
        SDL_Color errorColor = {255, 255, 255, 255}; // White text
        SDL_Color bgColor = {255, 0, 0, 180};        // Semi-transparent red background

        // Use larger font for error messages
        int errorFontScale = 400; // 400% = 4x larger
        m_textRenderer->setFontSize(errorFontScale);

        // Calculate actual font size for positioning
        int actualFontSize = static_cast<int>(baseFontSize * (errorFontScale / 100.0));

        // Split message into two lines if it's too long
        std::string line1, line2;
        int avgCharWidth = actualFontSize * 0.50;
        int maxCharsPerLine = (windowWidth - 60) / avgCharWidth;

        if (static_cast<int>(m_state.errorMessage.length()) <= maxCharsPerLine)
        {
            line1 = m_state.errorMessage;
        }
        else
        {
            // Split into two lines, preferably at a space
            size_t splitPos = m_state.errorMessage.length() / 2;
            size_t spacePos = m_state.errorMessage.find_last_of(' ', splitPos + 10);
            if (spacePos != std::string::npos && spacePos > splitPos - 10)
            {
                splitPos = spacePos;
            }

            line1 = m_state.errorMessage.substr(0, splitPos);
            line2 = m_state.errorMessage.substr(splitPos);

            // Trim leading space from second line
            if (!line2.empty() && line2[0] == ' ')
            {
                line2 = line2.substr(1);
            }
        }

        // Calculate dimensions and draw background
        int maxLineWidth = std::max(static_cast<int>(line1.length()), static_cast<int>(line2.length())) * avgCharWidth;
        int totalHeight = line2.empty() ? actualFontSize : (actualFontSize * 2 + 10);
        int messageX = (windowWidth - maxLineWidth) / 2;
        int messageY = (windowHeight - totalHeight) / 2;

        int bgExtension = windowWidth * 0.1;
        SDL_Rect bgRect = {messageX - 20 - bgExtension / 2, messageY - 10, maxLineWidth + 60 + bgExtension, totalHeight + 20};
        SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        SDL_SetRenderDrawBlendMode(m_renderer->getSDLRenderer(), SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(m_renderer->getSDLRenderer(), &bgRect);

        // Draw text lines
        int line1Width = static_cast<int>(line1.length()) * avgCharWidth;
        int line1X = (windowWidth - line1Width) / 2 + (windowWidth * 0.05);
        m_textRenderer->renderText(line1, line1X, messageY, errorColor);

        if (!line2.empty())
        {
            int line2Width = static_cast<int>(line2.length()) * avgCharWidth;
            int line2X = (windowWidth - line2Width) / 2 + (windowWidth * 0.05);
            int line2Y = messageY + actualFontSize + 10;
            m_textRenderer->renderText(line2, line2X, line2Y, errorColor);
        }

        // Restore original font size
        m_textRenderer->setFontSize(100);
    }
    else if (!m_state.errorMessage.empty())
    {
        // Clear expired error message
        m_state.errorMessage.clear();
    }
}

void RenderManager::renderPageJumpInput(NavigationManager* navigationManager, int windowWidth, int windowHeight)
{
    if (navigationManager->isPageJumpInputActive())
    {
        int baseFontSize = 16;
        SDL_Color jumpColor = {255, 255, 255, 255}; // White text
        SDL_Color jumpBgColor = {0, 100, 200, 200}; // Semi-transparent blue background

        // Use larger font for page jump input
        int jumpFontScale = 300; // 300% = 3x larger
        m_textRenderer->setFontSize(jumpFontScale);

        // Calculate actual font size for positioning
        int actualFontSize = static_cast<int>(baseFontSize * (jumpFontScale / 100.0));

        std::string jumpPrompt = "Go to page: " + navigationManager->getPageJumpBuffer() + "_";
        std::string jumpHint = "Enter page number (1-" + std::to_string(navigationManager->getPageCount()) + "), press Enter to confirm, Esc to cancel";

        // Calculate positioning
        int avgCharWidth = actualFontSize * 0.6;
        int promptWidth = static_cast<int>(jumpPrompt.length()) * avgCharWidth;
        int hintWidth = static_cast<int>(jumpHint.length()) * (actualFontSize / 2);

        int promptX = (windowWidth - promptWidth) / 2;
        int promptY = (windowHeight - actualFontSize * 2) / 2;

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
        int hintX = (windowWidth - hintWidth) / 2;
        int hintY = promptY + actualFontSize + 10;
        m_textRenderer->renderText(jumpHint, hintX, hintY, jumpColor);

        // Restore original font size
        m_textRenderer->setFontSize(100);
    }
}

void RenderManager::showErrorMessage(const std::string& message)
{
    m_state.errorMessage = message;
    m_state.errorMessageTime = SDL_GetTicks();
    markDirty();
}

void RenderManager::renderEdgeTurnProgressIndicator(App* app, NavigationManager* navigationManager,
                                                     ViewportManager* viewportManager, int windowWidth, int windowHeight)
{
    // Get edge turn state from app
    float edgeTurnHoldRight = app->getEdgeTurnHoldRight();
    float edgeTurnHoldLeft = app->getEdgeTurnHoldLeft();
    float edgeTurnHoldUp = app->getEdgeTurnHoldUp();
    float edgeTurnHoldDown = app->getEdgeTurnHoldDown();
    float edgeTurnThreshold = app->getEdgeTurnThreshold();
    
    bool dpadRightHeld = app->isDpadRightHeld();
    bool dpadLeftHeld = app->isDpadLeftHeld();
    bool dpadUpHeld = app->isDpadUpHeld();
    bool dpadDownHeld = app->isDpadDownHeld();
    
    // Check if any d-pad button is held and there's active edge turn timing
    bool dpadHeld = dpadLeftHeld || dpadRightHeld || dpadUpHeld || dpadDownHeld;
    float maxEdgeHold = std::max({edgeTurnHoldRight, edgeTurnHoldLeft, edgeTurnHoldUp, edgeTurnHoldDown});
    
    // Get scroll limits to check if content is scrollable
    int pageWidth = viewportManager->getPageWidth();
    int pageHeight = viewportManager->getPageHeight();
    int maxScrollX = std::max(0, (pageWidth - windowWidth) / 2);
    int maxScrollY = std::max(0, (pageHeight - windowHeight) / 2);
    
    // Check if there are valid pages to navigate to in each direction
    int currentPage = navigationManager->getCurrentPage();
    int pageCount = navigationManager->getPageCount();
    bool canGoLeft = currentPage > 0;
    bool canGoRight = currentPage < pageCount - 1;
    bool canGoUp = currentPage > 0;
    bool canGoDown = currentPage < pageCount - 1;
    
    // Only show progress bar when content doesn't fit in the movement direction
    // AND there's a valid page to navigate to
    bool validDirection = false;
    if (dpadRightHeld && edgeTurnHoldRight > 0.0f && canGoRight && maxScrollX > 0) {
        validDirection = true;
    }
    if (dpadLeftHeld && edgeTurnHoldLeft > 0.0f && canGoLeft && maxScrollX > 0) {
        validDirection = true;
    }
    if (dpadDownHeld && edgeTurnHoldDown > 0.0f && canGoDown && maxScrollY > 0) {
        validDirection = true;
    }
    if (dpadUpHeld && edgeTurnHoldUp > 0.0f && canGoUp && maxScrollY > 0) {
        validDirection = true;
    }
    
    if (dpadHeld && maxEdgeHold > 0.0f && validDirection) {
        float progress = maxEdgeHold / edgeTurnThreshold;
        
        // Only show indicator after 5% progress to avoid flicker
        if (progress > 0.05f) {
            // Enhance progress visualization for better completion feedback
            float visualProgress = std::min(progress * 1.1f, 1.0f);
            
            // Determine which edge and direction
            std::string direction;
            int indicatorX = 0, indicatorY = 0;
            int barWidth = 200, barHeight = 20;
            
            if (edgeTurnHoldRight > 0.0f && dpadRightHeld) {
                direction = "Next Page";
                indicatorX = windowWidth - barWidth - 20;
                indicatorY = windowHeight / 2;
            } else if (edgeTurnHoldLeft > 0.0f && dpadLeftHeld) {
                direction = "Previous Page";
                indicatorX = 20;
                indicatorY = windowHeight / 2;
            } else if (edgeTurnHoldDown > 0.0f && dpadDownHeld) {
                direction = "Next Page";
                indicatorX = (windowWidth - barWidth) / 2;
                indicatorY = windowHeight - 60;
            } else if (edgeTurnHoldUp > 0.0f && dpadUpHeld) {
                direction = "Previous Page";
                indicatorX = (windowWidth - barWidth) / 2;
                indicatorY = 40;
            }
            
            // Calculate text dimensions for better background sizing
            int avgCharWidth = 10;
            int textWidth = static_cast<int>(direction.length()) * avgCharWidth;
            int textHeight = 20;
            int textPadding = 12;
            
            // Position text above the progress bar
            int textX = indicatorX + (barWidth - textWidth) / 2;
            int textY = indicatorY - textHeight - textPadding - 5;
            
            // Draw text background container
            SDL_Rect textBgRect = {
                textX - textPadding,
                textY - textPadding,
                textWidth + 2 * textPadding,
                textHeight + 2 * textPadding
            };
            SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), 0, 0, 0, 180);
            SDL_SetRenderDrawBlendMode(m_renderer->getSDLRenderer(), SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(m_renderer->getSDLRenderer(), &textBgRect);
            
            // Draw text background border
            SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), 255, 255, 255, 255);
            SDL_RenderDrawRect(m_renderer->getSDLRenderer(), &textBgRect);
            
            // Draw direction text
            m_textRenderer->setFontSize(120);
            SDL_Color textColor = {255, 255, 255, 255};
            m_textRenderer->renderText(direction, textX, textY, textColor);
            m_textRenderer->setFontSize(100); // Restore default
            
            // Draw progress bar
            SDL_Color bgColor = {50, 50, 50, 200};
            SDL_Color fillColor = {0, 200, 0, 255};
            
            // Draw background
            SDL_Rect bgRect = {indicatorX, indicatorY, barWidth, barHeight};
            SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), bgColor.r, bgColor.g, bgColor.b, bgColor.a);
            SDL_SetRenderDrawBlendMode(m_renderer->getSDLRenderer(), SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(m_renderer->getSDLRenderer(), &bgRect);
            
            // Draw fill
            int fillWidth = static_cast<int>(barWidth * visualProgress);
            SDL_Rect fillRect = {indicatorX, indicatorY, fillWidth, barHeight};
            SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), fillColor.r, fillColor.g, fillColor.b, fillColor.a);
            SDL_RenderFillRect(m_renderer->getSDLRenderer(), &fillRect);
            
            // Draw border
            SDL_SetRenderDrawColor(m_renderer->getSDLRenderer(), 255, 255, 255, 255);
            SDL_RenderDrawRect(m_renderer->getSDLRenderer(), &bgRect);
        }
    }
}

void RenderManager::present()
{
    m_renderer->present();
}

uint32_t RenderManager::rgb24_to_argb32(uint8_t r, uint8_t g, uint8_t b)
{
    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}
