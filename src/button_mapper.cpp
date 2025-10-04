#include "button_mapper.h"
#include <iostream>

ButtonMapper::ButtonMapper()
{
    initializePlatformMappings();
}

void ButtonMapper::initializePlatformMappings()
{
    // Common mappings (same across all platforms)
    m_buttonMap[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = LogicalButton::PagePrevious;
    m_buttonMap[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = LogicalButton::PageNext;
    m_buttonMap[SDL_CONTROLLER_BUTTON_BACK] = LogicalButton::Options;
    m_buttonMap[SDL_CONTROLLER_BUTTON_GUIDE] = LogicalButton::Quit;
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_UP] = LogicalButton::DPadUp;
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = LogicalButton::DPadDown;
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = LogicalButton::DPadLeft;
    m_buttonMap[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = LogicalButton::DPadRight;

#ifdef TG5040_PLATFORM
    // TG5040 platform: Controller is detected as Xbox 360, but physical buttons are different
    // We keep the ORIGINAL behavior - SDL reports them "wrong" but the original code compensated
    // So we map them as-is without "fixing" the swap:
    // - BUTTON_A (SDL) -> mapped to Accept (this is how the original code worked)
    // - BUTTON_B (SDL) -> mapped to Cancel (this is how the original code worked)

    m_buttonMap[SDL_CONTROLLER_BUTTON_A] = LogicalButton::Accept;    // Keep original behavior
    m_buttonMap[SDL_CONTROLLER_BUTTON_B] = LogicalButton::Cancel;    // Keep original behavior
    m_buttonMap[SDL_CONTROLLER_BUTTON_X] = LogicalButton::Alternate; // X button
    m_buttonMap[SDL_CONTROLLER_BUTTON_Y] = LogicalButton::Special;   // Y button
    m_buttonMap[SDL_CONTROLLER_BUTTON_START] = LogicalButton::MirrorHorizontal; // TG5040: Start toggles mirror

    // TG5040-specific extra buttons (not part of standard SDL GameController API)
    m_joystickButtonMap[9] = LogicalButton::Extra1;  // Button 9: Reset View
    m_joystickButtonMap[10] = LogicalButton::Extra2; // Button 10: Toggle Menu

    std::cout << "ButtonMapper: Initialized TG5040 mappings (original behavior maintained)" << std::endl;
#else
    // Desktop platforms: Standard Xbox 360 controller layout
    // Physical layout matches SDL mapping:
    // - Physical "A" button (bottom) -> SDL reports as BUTTON_A
    // - Physical "B" button (right) -> SDL reports as BUTTON_B

    // Map joystick buttons for Share button (various controllers report it differently)
    m_joystickButtonMap[5] = LogicalButton::Menu;  // Common Share button mapping (misc1:b5)
    m_joystickButtonMap[15] = LogicalButton::Menu; // Fallback for other controllers

    m_buttonMap[SDL_CONTROLLER_BUTTON_A] = LogicalButton::Accept;    // A (bottom) -> Accept
    m_buttonMap[SDL_CONTROLLER_BUTTON_B] = LogicalButton::Cancel;    // B (right) -> Cancel
    m_buttonMap[SDL_CONTROLLER_BUTTON_X] = LogicalButton::Alternate; // X (left)
    m_buttonMap[SDL_CONTROLLER_BUTTON_Y] = LogicalButton::Special;   // Y (top)
    m_buttonMap[SDL_CONTROLLER_BUTTON_START] = LogicalButton::Menu;  // Desktop: Start opens menu

    // Desktop-specific: Share/Capture button opens menu (SDL 2.0.14+)
    // Note: On macOS, the Share button may be intercepted by the system
#if SDL_VERSION_ATLEAST(2, 0, 14)
    m_buttonMap[SDL_CONTROLLER_BUTTON_MISC1] = LogicalButton::Menu;
#endif

    std::cout << "ButtonMapper: Initialized desktop mappings" << std::endl;
#endif
}

LogicalButton ButtonMapper::mapButton(SDL_GameControllerButton physicalButton) const
{
    auto it = m_buttonMap.find(physicalButton);
    if (it != m_buttonMap.end())
    {
        // DEBUG: Log START button mapping
        if (physicalButton == SDL_CONTROLLER_BUTTON_START)
        {
            std::cout << "[ButtonMapper] START button mapped to LogicalButton::"
                      << (it->second == LogicalButton::Menu ? "Menu" :
                          it->second == LogicalButton::MirrorHorizontal ? "MirrorHorizontal" : "Unknown")
                      << " (enum value " << static_cast<int>(it->second) << ")" << std::endl;
        }
        return it->second;
    }

    // Return Accept as default fallback
    return LogicalButton::Accept;
}

bool ButtonMapper::isButton(SDL_GameControllerButton physicalButton, LogicalButton logicalButton) const
{
    auto it = m_buttonMap.find(physicalButton);
    if (it != m_buttonMap.end())
    {
        return it->second == logicalButton;
    }

    return false;
}

LogicalButton ButtonMapper::mapJoystickButton(int joystickButton) const
{
    auto it = m_joystickButtonMap.find(joystickButton);
    if (it != m_joystickButtonMap.end())
    {
        return it->second;
    }

    // Return Accept as default fallback
    return LogicalButton::Accept;
}

const char* ButtonMapper::getPlatformName() const
{
#ifdef TG5040_PLATFORM
    return "TG5040";
#else
    return "Desktop";
#endif
}
