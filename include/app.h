#ifndef APP_H
#define APP_H

#include "document.h"
#include "gui_manager.h"
using GuiManagerType = GuiManager;
#include "input_manager.h"
#include "navigation_manager.h"
#include "options_manager.h"
#include "reading_history_manager.h"
#include "render_manager.h"
#include "renderer.h"
#include "text_renderer.h"
#include "viewport_manager.h"
#if defined(TRIMUI_PLATFORM) || defined(PLATFORM_MLP1)
#include "power_handler.h"
#endif

#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct AppLaunchOptions
{
    bool forceShowImagesInFileBrowser{false};
};

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
    App(const std::string& filename, SDL_Window* window, SDL_Renderer* renderer, AppLaunchOptions launchOptions = {});
    ~App();

    void run();

    // Get document mutex for thread-safe access
    std::mutex& getDocumentMutex()
    {
        return m_documentMutex;
    }

    // Edge turn state getters for rendering
    float getEdgeTurnHoldRight() const
    {
        return m_edgeTurnHoldRight;
    }
    float getEdgeTurnHoldLeft() const
    {
        return m_edgeTurnHoldLeft;
    }
    float getEdgeTurnHoldUp() const
    {
        return m_edgeTurnHoldUp;
    }
    float getEdgeTurnHoldDown() const
    {
        return m_edgeTurnHoldDown;
    }
    float getEdgeTurnThreshold() const
    {
        return static_cast<float>(std::max(0, m_cachedConfig.edgeTurnHoldDurationMs)) / 1000.0f;
    }
    bool isDpadLeftHeld() const
    {
        return m_dpadLeftHeld;
    }
    bool isDpadRightHeld() const
    {
        return m_dpadRightHeld;
    }
    bool isDpadUpHeld() const
    {
        return m_dpadUpHeld;
    }
    bool isDpadDownHeld() const
    {
        return m_dpadDownHeld;
    }
    bool shouldShowEdgeTurnProgressBar() const
    {
        return m_cachedConfig.edgePageTurnsMode == EdgePageTurnsMode::Automatic &&
               m_cachedConfig.edgeTurnHoldDurationMs > 0;
    }

private:
    enum class EdgeDirection
    {
        None = 0,
        Right,
        Left,
        Up,
        Down
    };

    // Document Management
    void loadDocument();
    void refreshPageCountFromDocument();

    // Event Handling
    void handleEvent(const SDL_Event& event);
#if defined(TRIMUI_PLATFORM) || defined(PLATFORM_MLP1)
    void handlePowerMessageEvent(const SDL_Event& event);
#endif
    void processInputAction(const InputActionData& actionData);
    void updateInputState(const SDL_Event& event);

    // Font management
    void applyPendingFontChange(); // Apply deferred font configuration changes safely

    // State Management
    void printAppState();

    // Font management
    void toggleFontMenu();
    void applyFontConfiguration(const FontConfig& config);
    bool saveConfigWithRuntimeOverrides(const FontConfig& config);

    // Game controller management
    void initializeGameControllers();
    void closeGameControllers();

    // DPad nudge methods
    void handleDpadNudgeRight();
    void handleDpadNudgeLeft();
    void handleDpadNudgeUp();
    void handleDpadNudgeDown();
    bool performEdgeTurn(EdgeDirection direction);
    bool handleDoubleTapEdgePress(EdgeDirection direction);
    bool canTurnPageInDirection(EdgeDirection direction) const;
    bool isDirectionAtTurnEdge(EdgeDirection direction) const;
    bool isEligibleEdgeTurnDirection(EdgeDirection direction) const;
    void armDoubleTapEdgeTurnDirection(EdgeDirection direction);
    void clearDoubleTapEdgeTurnState();
    void clearDoubleTapEdgeTurnStateIfInvalid();
    void resetEdgeTurnHolds();
    void resetEdgeTurnProgressForDirection(EdgeDirection direction, bool startCooldown);
    float& edgeTurnHoldForDirection(EdgeDirection direction);
    float& edgeTurnCooldownForDirection(EdgeDirection direction);
    const float& edgeTurnHoldForDirection(EdgeDirection direction) const;

    // Pan speed (pixels per second)
    float m_dpadPanSpeed{600.0f};

    // Simple timestep
    Uint64 m_prevTick{0};

    bool m_running;
    AppLaunchOptions m_launchOptions;

    // Core managers
    std::unique_ptr<Document> m_document;
    std::unique_ptr<GuiManagerType> m_guiManager;
    std::unique_ptr<OptionsManager> m_optionsManager;
    std::unique_ptr<ReadingHistoryManager> m_readingHistoryManager;
    std::unique_ptr<InputManager> m_inputManager;
    std::unique_ptr<ViewportManager> m_viewportManager;
    std::unique_ptr<NavigationManager> m_navigationManager;
    std::unique_ptr<RenderManager> m_renderManager;

    // Essential input state variables (still needed by App for compatibility)
    bool m_isDragging{false};
    float m_lastTouchX{0.0f};
    float m_lastTouchY{0.0f};

    // D-pad held state for continuous input (shared by D-pad buttons and analog stick)
    bool m_dpadLeftHeld{false};
    bool m_dpadRightHeld{false};
    bool m_dpadUpHeld{false};
    bool m_dpadDownHeld{false};

    // Track whether each direction was set by a physical D-pad button press.
    // Prevents the analog stick axis handler from clearing held state that was
    // set by a D-pad button (the two input sources share m_dpad*Held).
    bool m_dpadRightButtonDown{false};
    bool m_dpadLeftButtonDown{false};
    bool m_dpadUpButtonDown{false};
    bool m_dpadDownButtonDown{false};
    bool m_keyboardLeftHeld{false};
    bool m_keyboardRightHeld{false};
    bool m_keyboardUpHeld{false};
    bool m_keyboardDownHeld{false};
    Sint16 m_leftStickX{0};
    Sint16 m_leftStickY{0};

    // Edge-turn timing for page changes at edges
    float m_edgeTurnHoldRight{0.0f};
    float m_edgeTurnHoldLeft{0.0f};
    float m_edgeTurnHoldUp{0.0f};
    float m_edgeTurnHoldDown{0.0f};
    float m_edgeTurnCooldownRight{0.0f};
    float m_edgeTurnCooldownLeft{0.0f};
    float m_edgeTurnCooldownUp{0.0f};
    float m_edgeTurnCooldownDown{0.0f};
    float m_edgeTurnCooldownDuration{0.5f}; // seconds to wait before allowing edge-turn again
    // Track whether an edge turn already fired during a sustained hold at auto-zoom (max == 0).
    // Prevents re-accumulation and progress bar flashing when the page fits in that dimension.
    bool m_edgeTurnFiredRight{false};
    bool m_edgeTurnFiredLeft{false};
    bool m_edgeTurnFiredUp{false};
    bool m_edgeTurnFiredDown{false};
    EdgeDirection m_doubleTapArmedDirection{EdgeDirection::None};

    // Game controller support
    SDL_GameController* m_gameController{nullptr};
    SDL_JoystickID m_gameControllerInstanceID{-1};

    // Mutex to protect document access from multiple threads
    mutable std::mutex m_documentMutex;

#if defined(TRIMUI_PLATFORM) || defined(PLATFORM_MLP1)
    std::unique_ptr<PowerHandler> m_powerHandler;
    Uint32 m_powerMessageEventType{0};
#endif

    // Per-frame panning when D-pad is held
    bool updateHeldPanning(float dt);

    // Deferred font configuration change to avoid thread safety issues
    bool m_pendingFontChange{false};
    FontConfig m_pendingFontConfig;

    // After a CSS-based reopen, the page count is estimated from file size and
    // may be higher than the actual count.  We park on page 0 until the async
    // page count finalises, then navigate to min(desired, newCount-1).
    int m_pendingPageRestore{-1};

    // Cached configuration to avoid repeated file reads
    FontConfig m_cachedConfig;
    void refreshCachedConfig()
    {
        m_cachedConfig = m_optionsManager->loadConfig();
        resetEdgeTurnHolds();
        clearDoubleTapEdgeTurnState();
        if (m_renderManager)
        {
            m_renderManager->setShowMinimap(m_cachedConfig.showDocumentMinimap);
            m_renderManager->setShowPageIndicatorOverlay(m_cachedConfig.showPageIndicatorOverlay);
            m_renderManager->setShowScaleOverlay(m_cachedConfig.showScaleOverlay);
        }
    }

    // Document path for reading history
    std::string m_documentPath;

    // State variables still needed by App
    bool m_inFakeSleep{false};

    // Convenience methods
    void markDirty()
    {
        if (m_renderManager)
            m_renderManager->markDirty();
    }
    void showErrorMessage(const std::string& message)
    {
        if (m_renderManager)
            m_renderManager->showErrorMessage(message);
    }
    void updateScaleDisplayTime()
    {
        if (m_renderManager)
            m_renderManager->updateScaleDisplayTime();
    }
    void updatePageDisplayTime()
    {
        if (m_renderManager)
            m_renderManager->updatePageDisplayTime();
    }

    std::function<void(int)> makeSetCurrentPageCallback();
};

#endif // APP_H
