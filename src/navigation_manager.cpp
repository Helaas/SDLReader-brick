#include "navigation_manager.h"
#include "document.h"
#include "gui_manager.h"
#include "mupdf_document.h"
#include "viewport_manager.h"
#include <algorithm>
#include <iostream>
#include <string>

NavigationManager::NavigationManager()
{
    // Default state is already initialized in NavigationState
}

bool NavigationManager::goToNextPage(Document* document, ViewportManager* viewportManager, GuiManager* guiManager,
                                     std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                                     std::function<void()> updatePageDisplayCallback)
{
    if (m_state.currentPage < m_state.pageCount - 1)
    {
        performPageChange(m_state.currentPage + 1, document, viewportManager, guiManager,
                          markDirtyCallback, updateScaleDisplayCallback, updatePageDisplayCallback);
        return true;
    }
    return false;
}

bool NavigationManager::goToPreviousPage(Document* document, ViewportManager* viewportManager, GuiManager* guiManager,
                                         std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                                         std::function<void()> updatePageDisplayCallback)
{
    if (m_state.currentPage > 0)
    {
        performPageChange(m_state.currentPage - 1, document, viewportManager, guiManager,
                          markDirtyCallback, updateScaleDisplayCallback, updatePageDisplayCallback);
        return true;
    }
    return false;
}

bool NavigationManager::goToPage(int pageNum, Document* document, ViewportManager* viewportManager, GuiManager* guiManager,
                                 std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                                 std::function<void()> updatePageDisplayCallback)
{
    if (pageNum >= 0 && pageNum < m_state.pageCount)
    {
        performPageChange(pageNum, document, viewportManager, guiManager,
                          markDirtyCallback, updateScaleDisplayCallback, updatePageDisplayCallback);
        return true;
    }
    return false;
}

void NavigationManager::jumpPages(int delta, Document* document, ViewportManager* viewportManager, GuiManager* guiManager,
                                  std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                                  std::function<void()> updatePageDisplayCallback)
{
    int target = std::clamp(m_state.currentPage + delta, 0, m_state.pageCount - 1);
    goToPage(target, document, viewportManager, guiManager, markDirtyCallback, updateScaleDisplayCallback, updatePageDisplayCallback);
}

void NavigationManager::performPageChange(int newPage, Document* document, ViewportManager* viewportManager, GuiManager* guiManager,
                                          std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                                          std::function<void()> updatePageDisplayCallback)
{
    m_state.currentPage = newPage;

    if (viewportManager)
    {
        viewportManager->onPageChangedKeepZoom(document, m_state.currentPage);
        viewportManager->alignToTopOfCurrentPage();
    }

    if (updateScaleDisplayCallback)
    {
        updateScaleDisplayCallback();
    }

    if (updatePageDisplayCallback)
    {
        updatePageDisplayCallback();
    }

    if (markDirtyCallback)
    {
        markDirtyCallback();
    }

    // Cancel prerendering since we're changing pages
    if (document)
    {
        auto* muPdfDoc = dynamic_cast<MuPdfDocument*>(document);
        if (muPdfDoc)
        {
            muPdfDoc->cancelPrerendering();
        }
    }

    // Set cooldown timer to prevent rapid page changes during panning
    m_state.lastPageChangeTime = SDL_GetTicks();

    // Update GUI manager with current page
    if (guiManager)
    {
        guiManager->setCurrentPage(m_state.currentPage);
    }
}

void NavigationManager::startPageJumpInput()
{
    m_state.pageJumpInputActive = true;
    m_state.pageJumpBuffer.clear();
    m_state.pageJumpStartTime = SDL_GetTicks();
    std::cout << "Page jump mode activated. Enter page number (1-" << m_state.pageCount << ") and press Enter." << std::endl;
}

void NavigationManager::handlePageJumpInput(char digit)
{
    if (!m_state.pageJumpInputActive)
        return;

    // Check if we're still within timeout
    if (SDL_GetTicks() - m_state.pageJumpStartTime > NavigationState::PAGE_JUMP_TIMEOUT)
    {
        cancelPageJumpInput();
        return;
    }

    // Limit input length to prevent overflow
    if (m_state.pageJumpBuffer.length() < 10)
    {
        m_state.pageJumpBuffer += digit;
        std::cout << "Page jump input: " << m_state.pageJumpBuffer << std::endl;
    }
}

void NavigationManager::cancelPageJumpInput()
{
    if (m_state.pageJumpInputActive)
    {
        m_state.pageJumpInputActive = false;
        m_state.pageJumpBuffer.clear();
        std::cout << "Page jump cancelled." << std::endl;
    }
}

bool NavigationManager::confirmPageJumpInput(Document* document, ViewportManager* viewportManager, GuiManager* guiManager,
                                             std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                                             std::function<void()> updatePageDisplayCallback, std::function<void(const std::string&)> showErrorCallback)
{
    if (!m_state.pageJumpInputActive)
        return false;

    if (m_state.pageJumpBuffer.empty())
    {
        cancelPageJumpInput();
        return false;
    }

    try
    {
        int targetPage = std::stoi(m_state.pageJumpBuffer);

        // Convert from 1-based to 0-based indexing
        targetPage -= 1;

        if (targetPage >= 0 && targetPage < m_state.pageCount)
        {
            goToPage(targetPage, document, viewportManager, guiManager, markDirtyCallback, updateScaleDisplayCallback, updatePageDisplayCallback);
            std::cout << "Jumped to page " << (targetPage + 1) << std::endl;

            m_state.pageJumpInputActive = false;
            m_state.pageJumpBuffer.clear();
            return true;
        }
        else
        {
            std::cout << "Invalid page number. Valid range: 1-" << m_state.pageCount << std::endl;
            if (showErrorCallback)
            {
                showErrorCallback("Invalid page: " + m_state.pageJumpBuffer + ". Valid range: 1-" + std::to_string(m_state.pageCount));
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "Invalid page number format: " << m_state.pageJumpBuffer << std::endl;
        if (showErrorCallback)
        {
            showErrorCallback("Invalid page number: " + m_state.pageJumpBuffer);
        }
    }

    m_state.pageJumpInputActive = false;
    m_state.pageJumpBuffer.clear();
    return false;
}

bool NavigationManager::isInPageChangeCooldown() const
{
    return (SDL_GetTicks() - m_state.lastPageChangeTime) < NavigationState::PAGE_CHANGE_COOLDOWN;
}

bool NavigationManager::isInScrollTimeout() const
{
    return (SDL_GetTicks() - m_state.lastPageChangeTime) < (m_state.lastRenderDuration + 50);
}

bool NavigationManager::isNextRenderLikelyExpensive() const
{
    return m_state.lastRenderDuration > NavigationState::EXPENSIVE_RENDER_THRESHOLD_MS;
}

void NavigationManager::printNavigationState() const
{
    std::cout << "--- Navigation State ---" << std::endl;
    std::cout << "Current Page: " << (m_state.currentPage + 1) << "/" << m_state.pageCount << std::endl;
    std::cout << "Page Jump Active: " << (m_state.pageJumpInputActive ? "Yes" : "No") << std::endl;
    if (m_state.pageJumpInputActive)
    {
        std::cout << "Page Jump Buffer: '" << m_state.pageJumpBuffer << "'" << std::endl;
    }
    std::cout << "In Page Change Cooldown: " << (isInPageChangeCooldown() ? "Yes" : "No") << std::endl;
    std::cout << "Last Render Duration: " << m_state.lastRenderDuration << "ms" << std::endl;
    std::cout << "Next Render Likely Expensive: " << (isNextRenderLikelyExpensive() ? "Yes" : "No") << std::endl;
    std::cout << "------------------------" << std::endl;
}
