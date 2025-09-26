#ifndef APP_H
#define APP_H

#include "renderer.h"
#include "document.h"
#include "text_renderer.h"
#include "gui_manager.h"
#include "options_manager.h"
#include "input_manager.h"
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
        DragEnd,
        ToggleFontMenu
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
    void processInputAction(const InputActionData& actionData);
    void updateInputState(const SDL_Event& event);

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
    void applyPendingFontChange(); // Apply deferred font configuration changes safely
    void fitPageToWindow();
    void recenterScrollOnZoom(int oldScale, int newScale);
    void fitPageToWidth();

    // Scrolling
    void clampScroll();

    // State Management
    void resetPageView();
    void printAppState();
    
    // Cooldown and timing checks
    bool isInPageChangeCooldown() const;
    bool isInScrollTimeout() const;

    // Rotation and mirroring
    void rotateClockwise();
    void toggleMirrorVertical();
    void toggleMirrorHorizontal();

    // Font management
    void toggleFontMenu();
    void applyFontConfiguration(const FontConfig& config);
    
    // Game controller management
    void initializeGameControllers();
    void closeGameControllers();
    
    // DPad nudge methods
    void handleDpadNudgeRight();
    void handleDpadNudgeLeft();
    void handleDpadNudgeUp();
    void handleDpadNudgeDown();

    // Pan speed (pixels per second)
    float m_dpadPanSpeed{600.0f};

    // Simple timestep
    Uint64 m_prevTick{0};

    bool m_running;

    // Renderer now owns a unique_ptr to Renderer, which internally holds raw pointers
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Document> m_document;
    std::unique_ptr<TextRenderer> m_textRenderer;
    std::unique_ptr<GuiManager> m_guiManager;
    std::unique_ptr<OptionsManager> m_optionsManager;
    std::unique_ptr<InputManager> m_inputManager;
    
    // Essential input state variables (still needed by App for compatibility)
    bool m_isDragging{false};
    float m_lastTouchX{0.0f};
    float m_lastTouchY{0.0f};
    bool m_pageJumpInputActive{false};
    std::string m_pageJumpBuffer;
    Uint32 m_pageJumpStartTime{0};
    static constexpr Uint32 PAGE_JUMP_TIMEOUT = 5000; // 5 seconds
    
    // D-pad held state for continuous input
    bool m_dpadLeftHeld{false};
    bool m_dpadRightHeld{false};
    bool m_dpadUpHeld{false};
    bool m_dpadDownHeld{false};
    bool m_keyboardLeftHeld{false};
    bool m_keyboardRightHeld{false};
    bool m_keyboardUpHeld{false};
    bool m_keyboardDownHeld{false};
    
    // Edge-turn timing for page changes at edges
    float m_edgeTurnHoldRight{0.0f};
    float m_edgeTurnHoldLeft{0.0f};
    float m_edgeTurnHoldUp{0.0f};
    float m_edgeTurnHoldDown{0.0f};
    float m_edgeTurnThreshold{0.300f}; // seconds to hold at edge before page turn
    float m_edgeTurnCooldownRight{0.0f};
    float m_edgeTurnCooldownLeft{0.0f};
    float m_edgeTurnCooldownUp{0.0f};
    float m_edgeTurnCooldownDown{0.0f};
    float m_edgeTurnCooldownDuration{0.5f}; // seconds to wait before allowing edge-turn again
    
    // Game controller support
    SDL_GameController* m_gameController{nullptr};
    SDL_JoystickID m_gameControllerInstanceID{-1};
    
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



    // Per-frame panning when D-pad is held
    bool updateHeldPanning(float dt);

    // Page change cooldown to prevent rapid page flipping during panning
    Uint32 m_lastPageChangeTime{0};
    static constexpr Uint32 PAGE_CHANGE_COOLDOWN = 300; // 300ms cooldown after page change
    
    // Dynamic timeout based on actual rendering time (used for both scroll timeout and zoom debouncing)
    Uint32 m_lastRenderDuration{300}; // Default to 300ms if no render time measured yet
    
    // Threshold for determining when rendering becomes expensive enough to warrant immediate processing indicator
    static constexpr Uint32 EXPENSIVE_RENDER_THRESHOLD_MS = 200; // Show immediate indicator if last render took > 200ms
    

    
    // Check if next render is likely to be expensive based on recent rendering performance
    bool isNextRenderLikelyExpensive() const {
        return m_lastRenderDuration > EXPENSIVE_RENDER_THRESHOLD_MS;
    }
    
    // Check if zoom processing indicator should remain visible for minimum time
    bool shouldShowZoomProcessingIndicator() const {
        if (!m_zoomProcessing) return false;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_zoomProcessingStartTime).count();
        return elapsed < ZOOM_PROCESSING_MIN_DISPLAY_MS || isZoomDebouncing();
    }

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
    

    
    // Scale display timing
    Uint32 m_scaleDisplayTime{0};
    static constexpr Uint32 SCALE_DISPLAY_DURATION = 2000; // 2 seconds
    
    // Page display timing
    Uint32 m_pageDisplayTime{0};
    static constexpr Uint32 PAGE_DISPLAY_DURATION = 2000; // 2 seconds
    
    // Zoom throttling to prevent rapid operations
    std::chrono::steady_clock::time_point m_lastZoomTime;
#ifdef TG5040_PLATFORM
    static constexpr int ZOOM_THROTTLE_MS = 30; // Slower throttle for TG5040
    static constexpr int ZOOM_DEBOUNCE_MS = 250; // Longer debounce for slow hardware
#else
    static constexpr int ZOOM_THROTTLE_MS = 25; // 25ms minimum between zoom operations for smoother response
    static constexpr int ZOOM_DEBOUNCE_MS = 75; // Faster for other platforms
#endif

    // Zoom debouncing for performance on slow machines
    int m_pendingZoomDelta{0};
    std::chrono::steady_clock::time_point m_lastZoomInputTime;
    
    // Zoom processing indicator
    bool m_zoomProcessing{false};
    std::chrono::steady_clock::time_point m_zoomProcessingStartTime;
    static constexpr int ZOOM_PROCESSING_MIN_DISPLAY_MS = 300; // Minimum time to show processing indicator
    
    // Deferred font configuration change to avoid thread safety issues
    bool m_pendingFontChange{false};
    FontConfig m_pendingFontConfig;
    
    // Rendering optimization
    bool m_needsRedraw{true}; // Flag to indicate when screen needs to be redrawn
    void markDirty() { m_needsRedraw = true; }
};

#endif // APP_H