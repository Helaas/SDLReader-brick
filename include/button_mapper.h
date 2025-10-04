#ifndef BUTTON_MAPPER_H
#define BUTTON_MAPPER_H

#include <SDL.h>
#include <map>

/**
 * @brief Logical button actions that can be mapped to physical buttons
 */
enum class LogicalButton
{
    Accept,           // Confirm/OK action (typically A button)
    Cancel,           // Cancel/Back action (typically B button)
    Alternate,        // Alternative action (typically X button)
    Special,          // Special action (typically Y button)
    PageNext,         // Next page (typically R shoulder)
    PagePrevious,     // Previous page (typically L shoulder)
    Menu,             // Open menu (typically Start on desktop)
    MirrorHorizontal, // Toggle mirror horizontal (TG5040 Start button)
    Options,          // Open options (typically Back/Select)
    Quit,             // Quit application (typically Guide)
    DPadUp,           // D-pad up
    DPadDown,         // D-pad down
    DPadLeft,         // D-pad left
    DPadRight,        // D-pad right
    Extra1,           // Extra button 1 (TG5040 button 9 - Reset View)
    Extra2            // Extra button 2 (TG5040 button 10 - Toggle Menu)
};

/**
 * @brief Manages platform-specific button mappings
 *
 * This class provides a mapping layer between physical controller buttons
 * and logical button actions. Different platforms can have different mappings,
 * particularly for TG5040 where A and B buttons are swapped compared to Xbox 360.
 */
class ButtonMapper
{
public:
    ButtonMapper();
    ~ButtonMapper() = default;

    /**
     * @brief Map a physical SDL controller button to a logical button action
     * @param physicalButton SDL controller button (e.g., SDL_CONTROLLER_BUTTON_A)
     * @return Logical button action
     */
    LogicalButton mapButton(SDL_GameControllerButton physicalButton) const;

    /**
     * @brief Map a physical SDL controller button with context awareness
     * @param physicalButton SDL controller button
     * @param inFontMenu true if the font menu is currently open (swaps A & B on TG5040)
     * @return Logical button action with context-specific mapping
     */
    LogicalButton mapButtonForContext(SDL_GameControllerButton physicalButton, bool inFontMenu) const;

    /**
     * @brief Map a joystick button (for extra buttons not in SDL GameController API)
     * @param joystickButton Raw joystick button number
     * @return Logical button action, or Accept as default
     */
    LogicalButton mapJoystickButton(int joystickButton) const;

    /**
     * @brief Check if a physical button maps to a specific logical button
     * @param physicalButton SDL controller button
     * @param logicalButton Logical button to check
     * @return true if the physical button maps to the logical button
     */
    bool isButton(SDL_GameControllerButton physicalButton, LogicalButton logicalButton) const;

    /**
     * @brief Get platform name for debugging
     */
    const char* getPlatformName() const;

private:
    std::map<SDL_GameControllerButton, LogicalButton> m_buttonMap;
    std::map<int, LogicalButton> m_joystickButtonMap;

    /**
     * @brief Initialize platform-specific button mappings
     */
    void initializePlatformMappings();
};

#endif // BUTTON_MAPPER_H
