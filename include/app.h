#ifndef APP_H
#define APP_H

#include "renderer.h"
#include "document.h"
#include "text_renderer.h"
#ifdef TG5040_PLATFORM
#include "power_handler.h"
#endif

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

class App
{
public:
    // Define an enum for high-level application actions
    enum class AppAction
    {
        None,
        Quit,
        Resize,
        ScrollUp,
        ScrollDown,
        ScrollLeft,
        ScrollRight,
        PageNext,
        PagePrevious,
        ZoomIn,
        ZoomOut,
        ToggleFullscreen,
        DragStart,
        DragEnd
    };

    // Constructor now accepts pre-initialized SDL_Window* and SDL_Renderer*
    App(const std::string &filename, SDL_Window *window, SDL_Renderer *renderer);
    ~App();

    void run();

    // Get document mutex for thread-safe access
    std::mutex& getDocumentMutex() { return m_documentMutex; }

private:
    // Document and Rendering Management
    void loadDocument();
    void renderCurrentPage();
    void renderUI();
    
    // Error message display
    void showErrorMessage(const std::string& message);
    
    // Scale display timing
    void updateScaleDisplayTime();
    
    // Page display timing
    void updatePageDisplayTime();

    // Event Handling
    void handleEvent(const SDL_Event &event);

    // Page Navigation
    void goToNextPage();
    void goToPreviousPage();
    void goToPage(int pageNum);
    void jumpPages(int delta);
    void startPageJumpInput();
    void handlePageJumpInput(char digit);
    void cancelPageJumpInput();
    void confirmPageJumpInput();

    // Zoom and Scaling
    void zoom(int delta);
    void zoomTo(int scale);
    void applyPendingZoom();  // Apply accumulated zoom changes
    bool isZoomDebouncing() const;  // Check if zoom is currently being debounced
    void fitPageToWindow();
    void recenterScrollOnZoom(int oldScale, int newScale);
    void fitPageToWidth();

    // Scrolling
    void clampScroll();

    // State Management
    void resetPageView();
    void printAppState();

    // Rotation and mirroring
    void rotateClockwise();
    void toggleMirrorVertical();
    void toggleMirrorHorizontal();

    // D-pad hold state
    bool m_dpadLeftHeld{false};
    bool m_dpadRightHeld{false};
    bool m_dpadUpHeld{false};
    bool m_dpadDownHeld{false};

    // Pan speed (pixels per second)
    float m_dpadPanSpeed{600.0f};

    // Simple timestep
    Uint64 m_prevTick{0};

    bool m_running;

    // Renderer now owns a unique_ptr to Renderer, which internally holds raw pointers
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Document> m_document;
    std::unique_ptr<TextRenderer> m_textRenderer;
    
    // Mutex to protect document access from multiple threads
    mutable std::mutex m_documentMutex;
    
#ifdef TG5040_PLATFORM
    std::unique_ptr<PowerHandler> m_powerHandler;
#endif

    int m_currentPage;
    int m_pageCount;
    int m_currentScale;
    int m_scrollX;
    int m_scrollY;
    int m_pageWidth;
    int m_pageHeight;

    bool m_isDragging;
    float m_lastTouchX;
    float m_lastTouchY;

    // Game Controller
    SDL_GameController *m_gameController;
    SDL_JoystickID m_gameControllerInstanceID;

    // Helper to initialize and close game controllers
    void initializeGameControllers();
    void closeGameControllers();

    // Per-frame panning when D-pad is held
    bool updateHeldPanning(float dt);

    // --- edge-turn timing ---
    float m_edgeTurnHoldRight{0.0f};
    float m_edgeTurnHoldLeft{0.0f};
    float m_edgeTurnHoldUp{0.0f};
    float m_edgeTurnHoldDown{0.0f};

    float m_edgeTurnThreshold{0.300f}; // seconds to dwell at edge before flipping
    
    // Cooldown timestamps to prevent immediate nudges after edge-turn cancellation
    float m_edgeTurnCooldownRight{0.0f};
    float m_edgeTurnCooldownLeft{0.0f};
    float m_edgeTurnCooldownUp{0.0f};
    float m_edgeTurnCooldownDown{0.0f};
    float m_edgeTurnCooldownDuration{0.5f}; // 500ms cooldown after cancellation

    // Page change cooldown to prevent rapid page flipping during panning
    Uint32 m_lastPageChangeTime{0};
    static constexpr Uint32 PAGE_CHANGE_COOLDOWN = 300; // 300ms cooldown after page change
    
    // Dynamic timeout based on actual rendering time (used for both scroll timeout and zoom debouncing)
    Uint32 m_lastRenderDuration{300}; // Default to 300ms if no render time measured yet
    
    // Check if we're in page change cooldown period
    bool isInPageChangeCooldown() const {
        return (SDL_GetTicks() - m_lastPageChangeTime) < PAGE_CHANGE_COOLDOWN;
    }
    
    // Check if we should block scrolling after a page change (using dynamic timeout with minimum)
    bool isInScrollTimeout() const {
        Uint32 minTimeout = 100; // Minimum 100ms timeout regardless of render time
        Uint32 timeoutDuration = std::max(minTimeout, m_lastRenderDuration);
        return (SDL_GetTicks() - m_lastPageChangeTime) < timeoutDuration;
    }

    // Try a one-shot nudge; if at the edge, flip the page instead.
    void handleDpadNudgeRight();
    void handleDpadNudgeLeft();
    void handleDpadNudgeUp();
    void handleDpadNudgeDown();

    // Scroll extents helpers
    int getMaxScrollX() const;
    int getMaxScrollY() const; // if you later want vertical logic

    // Recompute scaled page dims for current page at the existing zoom and clamp scroll.
    void onPageChangedKeepZoom();

    // Top alignment control when page height <= window height
    bool m_topAlignWhenFits{true};         // prefer top-align instead of centering when it fits
    bool m_forceTopAlignNextRender{false}; // one-shot flag after page changes

    // Align the current page so its top is visible
    void alignToTopOfCurrentPage();

    // --- View transforms ---
    int m_rotation{0}; // 0, 90, 180, 270
    bool m_mirrorH{false};
    bool m_mirrorV{false};

    // Effective (native) page size with rotation
    int effectiveNativeWidth() const;
    int effectiveNativeHeight() const;

    // Compose SDL flip flags
    SDL_RendererFlip currentFlipFlags() const;
    
    // Error message display
    std::string m_errorMessage;
    Uint32 m_errorMessageTime{0};
    static constexpr Uint32 ERROR_MESSAGE_DURATION = 3000; // 3 seconds
    
    // Fake sleep mode state
    bool m_inFakeSleep{false};
    
    // Page jump input state
    bool m_pageJumpInputActive{false};
    std::string m_pageJumpBuffer;
    Uint32 m_pageJumpStartTime{0};
    static constexpr Uint32 PAGE_JUMP_TIMEOUT = 5000; // 5 seconds timeout
    
    // Scale display timing
    Uint32 m_scaleDisplayTime{0};
    static constexpr Uint32 SCALE_DISPLAY_DURATION = 2000; // 2 seconds
    
    // Page display timing
    Uint32 m_pageDisplayTime{0};
    static constexpr Uint32 PAGE_DISPLAY_DURATION = 2000; // 2 seconds
    
    // Zoom throttling to prevent rapid operations
    std::chrono::steady_clock::time_point m_lastZoomTime;
#ifdef TG5040_PLATFORM
    static constexpr int ZOOM_THROTTLE_MS = 50; // Slower throttle for TG5040
    static constexpr int ZOOM_DEBOUNCE_MS = 150; // Longer debounce for slow hardware
#else
    static constexpr int ZOOM_THROTTLE_MS = 25; // 25ms minimum between zoom operations for smoother response
    static constexpr int ZOOM_DEBOUNCE_MS = 75; // Faster for other platforms
#endif

    // Zoom debouncing for performance on slow machines
    int m_pendingZoomDelta{0};
    std::chrono::steady_clock::time_point m_lastZoomInputTime;
    
    // Zoom processing indicators
    bool m_zoomProcessing{false};
    std::chrono::steady_clock::time_point m_zoomProcessingStartTime;
    
    // Rendering optimization
    bool m_needsRedraw{true}; // Flag to indicate when screen needs to be redrawn
    void markDirty() { m_needsRedraw = true; }
};

#endif // APP_H