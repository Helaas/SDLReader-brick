#ifndef APP_H
#define APP_H

#include "renderer.h"
#include "document.h"
#include "text_renderer.h"
#include "gui_manager.h"
#include "options_manager.h"
#include "input_manager.h"
#include "viewport_manager.h"
#include "navigation_manager.h"
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

    // Renderer now owns a unique_ptr to Renderer, which internally holds raw pointers
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Document> m_document;
    std::unique_ptr<TextRenderer> m_textRenderer;
    std::unique_ptr<GuiManager> m_guiManager;
    std::unique_ptr<OptionsManager> m_optionsManager;
    std::unique_ptr<InputManager> m_inputManager;
    std::unique_ptr<ViewportManager> m_viewportManager;
    std::unique_ptr<NavigationManager> m_navigationManager;
    
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
    

    
    // Deferred font configuration change to avoid thread safety issues
    bool m_pendingFontChange{false};
    FontConfig m_pendingFontConfig;
    
    // Rendering optimization
    bool m_needsRedraw{true}; // Flag to indicate when screen needs to be redrawn
    void markDirty() { m_needsRedraw = true; }
};

#endif // APP_H