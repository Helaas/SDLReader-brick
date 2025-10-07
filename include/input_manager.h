#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include "button_mapper.h"
#include <SDL.h>
#include <chrono>
#include <functional>
#include <string>

/**
 * @brief Actions that can be triggered by input events
 */
enum class InputAction
{
    None,
    Quit,
    Resize,
    ToggleFontMenu,
    GoToNextPage,
    GoToPreviousPage,
    GoToPage,
    ZoomIn,
    ZoomOut,
    ZoomTo,
    GoToFirstPage,
    GoToLastPage,
    ResetPageView,
    RotateClockwise,
    ToggleMirrorHorizontal,
    ToggleMirrorVertical,
    FitPageToWindow,
    FitPageToWidth,
    ToggleFullscreen,
    JumpPages,
    PrintAppState,
    ClampScroll,
    StartPageJumpInput,
    HandlePageJumpInput,
    ConfirmPageJumpInput,
    CancelPageJumpInput,
    // Directional movement
    MoveRight,
    MoveLeft,
    MoveUp,
    MoveDown,
    // Mouse/touch
    StartDragging,
    StopDragging,
    UpdateDragging,
    // Scroll
    ScrollUp,
    ScrollDown
};

/**
 * @brief Structure to hold input action data
 */
struct InputActionData
{
    InputAction action = InputAction::None;
    int intValue = 0;        // For page numbers, zoom values, etc.
    float floatValue = 0.0f; // For deltas, positions, etc.
    char charValue = 0;      // For character input
    float deltaX = 0.0f;     // For mouse/touch movement
    float deltaY = 0.0f;     // For mouse/touch movement
    bool isPressed = false;  // For key/button state
};

/**
 * @brief Manages all input handling for the application
 */
class InputManager
{
public:
    InputManager();
    ~InputManager() = default;

    /**
     * @brief Process SDL event and convert to InputAction
     * @param event SDL event to process
     * @return InputActionData with action and associated data
     */
    InputActionData processEvent(const SDL_Event& event);

    /**
     * @brief Update held input states (for continuous actions like panning)
     * @param dt Delta time in seconds
     * @return InputActionData for continuous actions, or None if no action
     */
    InputActionData updateHeldInputs(float dt);

    /**
     * @brief Set page jump input as active/inactive
     */
    void setPageJumpActive(bool active)
    {
        m_pageJumpActive = active;
    }
    bool isPageJumpActive() const
    {
        return m_pageJumpActive;
    }

    /**
     * @brief Set current zoom step for zoom actions
     */
    void setZoomStep(int zoomStep)
    {
        m_zoomStep = zoomStep;
    }

    /**
     * @brief Set page count for validation
     */
    void setPageCount(int pageCount)
    {
        m_pageCount = pageCount;
    }

    /**
     * @brief Check if we're in page change cooldown
     */
    bool isInPageChangeCooldown() const;

    /**
     * @brief Check if we're in scroll timeout
     */
    bool isInScrollTimeout() const;

    /**
     * @brief Set page change cooldown (called after page changes)
     */
    void setPageChangeCooldown();

    /**
     * @brief Set scroll timeout (called after scroll actions)
     */
    void setScrollTimeout();

    /**
     * @brief Initialize game controllers
     */
    void initializeGameControllers();

    /**
     * @brief Close game controllers
     */
    void closeGameControllers();

    /**
     * @brief Get current input state for UI purposes
     */
    struct InputState
    {
        bool keyboardLeftHeld = false;
        bool keyboardRightHeld = false;
        bool keyboardUpHeld = false;
        bool keyboardDownHeld = false;
        bool dpadLeftHeld = false;
        bool dpadRightHeld = false;
        bool dpadUpHeld = false;
        bool dpadDownHeld = false;
        bool isDragging = false;
        float lastTouchX = 0.0f;
        float lastTouchY = 0.0f;

        // Edge turn state
        float edgeTurnHoldRight = 0.0f;
        float edgeTurnHoldLeft = 0.0f;
        float edgeTurnHoldUp = 0.0f;
        float edgeTurnHoldDown = 0.0f;
        float edgeTurnCooldownRight = 0.0f;
        float edgeTurnCooldownLeft = 0.0f;
        float edgeTurnCooldownUp = 0.0f;
        float edgeTurnCooldownDown = 0.0f;
    };

    const InputState& getInputState() const
    {
        return m_inputState;
    }

    /**
     * @brief Get button mapper for platform-specific button mapping
     */
    const ButtonMapper& getButtonMapper() const
    {
        return m_buttonMapper;
    }

private:
    // Input state
    InputState m_inputState;

    // Button mapper for platform-specific button mapping
    ButtonMapper m_buttonMapper;

    // Page jump state
    bool m_pageJumpActive = false;
    std::string m_pageJumpBuffer;

    // Configuration
    int m_zoomStep = 10;
    int m_pageCount = 1;

    // Cooldowns and timeouts
    Uint32 m_lastPageChangeTime = 0;
    Uint32 m_lastScrollTime = 0;

    // Game controller
    SDL_GameController* m_gameController = nullptr;
    SDL_JoystickID m_gameControllerInstanceID = -1;

    // Constants
    static constexpr Uint32 PAGE_CHANGE_COOLDOWN_MS = 200;
    static constexpr Uint32 SCROLL_TIMEOUT_MS = 100;
    static constexpr Uint32 PAGE_JUMP_TIMEOUT = 5000;
    static constexpr float EDGE_TURN_THRESHOLD = 1.5f;
    static constexpr float EDGE_TURN_COOLDOWN_TIME = 0.5f;
    static constexpr int DPAD_NUDGE_AMOUNT = 20;

    // Helper methods
    InputActionData processKeyDown(const SDL_Event& event);
    InputActionData processKeyUp(const SDL_Event& event);
    InputActionData processControllerButton(const SDL_Event& event);
    InputActionData processControllerAxis(const SDL_Event& event);
    InputActionData processMouse(const SDL_Event& event);

    void handlePageJumpInput(char c);
    void updateEdgeTurnTimers(float dt);
    bool checkEdgeTurnTrigger();
};

#endif // INPUT_MANAGER_H
