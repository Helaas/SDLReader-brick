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
#ifdef TG5040_PLATFORM
#include "power_handler.h"
#endif

#include <SDL.h>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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
    App(const std::string& filename, SDL_Window* window, SDL_Renderer* renderer);
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
        return m_edgeTurnThreshold;
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
    bool isEdgeProgressBarDisabled() const
    {
        return m_cachedConfig.disableEdgeProgressBar;
    }

private:
    // Document Management
    void loadDocument();

    // Event Handling
    void handleEvent(const SDL_Event& event);
    void processInputAction(const InputActionData& actionData);
    void updateInputState(const SDL_Event& event);

    // Font management
    void applyPendingFontChange(); // Apply deferred font configuration changes safely

    // State Management
    void printAppState();

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

    // Per-frame panning when D-pad is held
    bool updateHeldPanning(float dt);

    // Deferred font configuration change to avoid thread safety issues
    bool m_pendingFontChange{false};
    FontConfig m_pendingFontConfig;

    // Cached configuration to avoid repeated file reads
    FontConfig m_cachedConfig;
    void refreshCachedConfig()
    {
        m_cachedConfig = m_optionsManager->loadConfig();
        if (m_renderManager)
        {
            m_renderManager->setShowMinimap(m_cachedConfig.showDocumentMinimap);
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
