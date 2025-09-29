#ifndef NAVIGATION_MANAGER_H
#define NAVIGATION_MANAGER_H

#include <SDL.h>
#include <functional>
#include <string>

// Forward declarations
class Document;
class MuPdfDocument;
class ViewportManager;
class BaseGuiManager;

/**
 * @brief Structure to hold navigation state
 */
struct NavigationState
{
    int currentPage = 0;
    int pageCount = 0;

    // Page jump input state
    bool pageJumpInputActive = false;
    std::string pageJumpBuffer;
    Uint32 pageJumpStartTime = 0;

    // Page change cooldown to prevent rapid page flipping
    Uint32 lastPageChangeTime = 0;
    Uint32 lastRenderDuration = 300; // Default to 300ms if no render time measured yet

    // Constants
    static constexpr Uint32 PAGE_JUMP_TIMEOUT = 5000;            // 5 seconds
    static constexpr Uint32 PAGE_CHANGE_COOLDOWN = 300;          // 300ms cooldown after page change
    static constexpr Uint32 EXPENSIVE_RENDER_THRESHOLD_MS = 200; // Show immediate indicator if last render took > 200ms
};

/**
 * @brief Manages document navigation including page changes, page jumping, and cooldown management
 */
class NavigationManager
{
public:
    NavigationManager();
    ~NavigationManager() = default;

    // State management
    void setPageCount(int pageCount)
    {
        m_state.pageCount = pageCount;
    }
    void setCurrentPage(int currentPage)
    {
        m_state.currentPage = currentPage;
    }
    void setLastRenderDuration(Uint32 duration)
    {
        m_state.lastRenderDuration = duration;
    }

    // State accessors
    int getCurrentPage() const
    {
        return m_state.currentPage;
    }
    int getPageCount() const
    {
        return m_state.pageCount;
    }
    bool isPageJumpInputActive() const
    {
        return m_state.pageJumpInputActive;
    }
    const std::string& getPageJumpBuffer() const
    {
        return m_state.pageJumpBuffer;
    }

    // Page navigation
    bool goToNextPage(Document* document, ViewportManager* viewportManager, BaseGuiManager* guiManager,
                      std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                      std::function<void()> updatePageDisplayCallback);
    bool goToPreviousPage(Document* document, ViewportManager* viewportManager, BaseGuiManager* guiManager,
                          std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                          std::function<void()> updatePageDisplayCallback);
    bool goToPage(int pageNum, Document* document, ViewportManager* viewportManager, BaseGuiManager* guiManager,
                  std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                  std::function<void()> updatePageDisplayCallback);
    void jumpPages(int delta, Document* document, ViewportManager* viewportManager, BaseGuiManager* guiManager,
                   std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                   std::function<void()> updatePageDisplayCallback);

    // Page jump input handling
    void startPageJumpInput();
    void handlePageJumpInput(char digit);
    void cancelPageJumpInput();
    bool confirmPageJumpInput(Document* document, ViewportManager* viewportManager, BaseGuiManager* guiManager,
                              std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                              std::function<void()> updatePageDisplayCallback, std::function<void(const std::string&)> showErrorCallback);

    // Cooldown and timing checks
    bool isInPageChangeCooldown() const;
    bool isInScrollTimeout() const;
    bool isNextRenderLikelyExpensive() const;

    // Utility methods
    void printNavigationState() const;

private:
    NavigationState m_state;

    // Internal page change implementation
    void performPageChange(int newPage, Document* document, ViewportManager* viewportManager, BaseGuiManager* guiManager,
                           std::function<void()> markDirtyCallback, std::function<void()> updateScaleDisplayCallback,
                           std::function<void()> updatePageDisplayCallback);
};

#endif // NAVIGATION_MANAGER_H
