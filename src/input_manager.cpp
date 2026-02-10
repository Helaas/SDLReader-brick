#include "input_manager.h"
#include <algorithm>
#include <iostream>

InputManager::InputManager()
{
    // Initialize game controllers
    initializeGameControllers();
}

InputActionData InputManager::processEvent(const SDL_Event& event)
{
    InputActionData actionData;

    switch (event.type)
    {
    case SDL_QUIT:
        actionData.action = InputAction::Quit;
        break;

    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
            event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
            actionData.action = InputAction::Resize;
        }
        break;

    case SDL_KEYDOWN:
        actionData = processKeyDown(event);
        break;

    case SDL_KEYUP:
        actionData = processKeyUp(event);
        break;

    case SDL_MOUSEWHEEL:
        if (event.wheel.y > 0)
        {
            if (SDL_GetModState() & KMOD_CTRL)
            {
                actionData.action = InputAction::ZoomIn;
                actionData.intValue = m_zoomStep;
            }
            else if (!isInScrollTimeout())
            {
                actionData.action = InputAction::ScrollUp;
                actionData.intValue = 50;
            }
        }
        else if (event.wheel.y < 0)
        {
            if (SDL_GetModState() & KMOD_CTRL)
            {
                actionData.action = InputAction::ZoomOut;
                actionData.intValue = m_zoomStep;
            }
            else if (!isInScrollTimeout())
            {
                actionData.action = InputAction::ScrollDown;
                actionData.intValue = 50;
            }
        }
        break;

    case SDL_MOUSEBUTTONDOWN:
        actionData = processMouse(event);
        break;

    case SDL_MOUSEBUTTONUP:
        actionData = processMouse(event);
        break;

    case SDL_MOUSEMOTION:
        actionData = processMouse(event);
        break;

    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        // On TG5040, joystick buttons 9 and 10 are extra buttons handled via SDL_JOYBUTTONDOWN
        // SDL maps these to controller buttons 7 and 8 - filter them out to prevent double-processing
#ifdef TRIMUI_PLATFORM
    {
        int buttonNum = event.cbutton.button;

        // Based on logs: joystick button 10 -> controller button 8
        // Likely: joystick button 9 -> controller button 7
        // These are Extra1 and Extra2, handled via SDL_JOYBUTTONDOWN
        if (buttonNum == 7 || buttonNum == 8)
        {
            break; // Don't process
        }
    }
#endif
        actionData = processControllerButton(event);
        break;

    case SDL_CONTROLLERAXISMOTION:
        actionData = processControllerAxis(event);
        break;

    case SDL_CONTROLLERDEVICEADDED:
        if (m_gameController == nullptr)
        {
            m_gameController = SDL_GameControllerOpen(event.cdevice.which);
            if (m_gameController)
            {
                m_gameControllerInstanceID = SDL_JoystickGetDeviceInstanceID(event.cdevice.which);
                const char* controllerName = SDL_GameControllerName(m_gameController);
                SDL_Joystick* joystick = SDL_GameControllerGetJoystick(m_gameController);
                int numButtons = SDL_JoystickNumButtons(joystick);

                std::cout << "Opened game controller: " << controllerName << std::endl;
                std::cout << "Controller has " << numButtons << " joystick buttons" << std::endl;
                std::cout << "Controller mapping: " << SDL_GameControllerMapping(m_gameController) << std::endl;
            }
            else
            {
                std::cerr << "Could not open game controller: " << SDL_GetError() << std::endl;
            }
        }
        break;

    case SDL_CONTROLLERDEVICEREMOVED:
        if (m_gameController != nullptr && event.cdevice.which == m_gameControllerInstanceID)
        {
            SDL_GameControllerClose(m_gameController);
            m_gameController = nullptr;
            m_gameControllerInstanceID = -1;
            std::cout << "Game controller disconnected." << std::endl;
        }
        break;

    case SDL_JOYBUTTONDOWN:
    {
        // Handle joystick buttons through mapper (for platform-specific extra buttons)
        LogicalButton logicalButton = m_buttonMapper.mapJoystickButton(event.jbutton.button);

        switch (logicalButton)
        {
        case LogicalButton::Extra1:
            actionData.action = InputAction::ResetPageView;
            break;
        case LogicalButton::Extra2:
            actionData.action = InputAction::ToggleFontMenu;
            break;
        case LogicalButton::Menu:
            actionData.action = InputAction::ToggleFontMenu;
            break;
        default:
            // No action for unmapped joystick buttons
            break;
        }
        break;
    }
    }

    return actionData;
}

InputActionData InputManager::processKeyDown(const SDL_Event& event)
{
    InputActionData actionData;

    switch (event.key.keysym.sym)
    {
    case SDLK_AC_HOME:
        actionData.action = InputAction::Quit;
        break;

    case SDLK_ESCAPE:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::CancelPageJumpInput;
        }
        else
        {
            actionData.action = InputAction::Quit;
        }
        break;

    case SDLK_q:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::CancelPageJumpInput;
        }
        else
        {
            actionData.action = InputAction::Quit;
        }
        break;

    case SDLK_RIGHT:
        m_inputState.keyboardRightHeld = true;
        // Let App handle edge-turn and nudge logic
        break;

    case SDLK_LEFT:
        m_inputState.keyboardLeftHeld = true;
        // Let App handle edge-turn and nudge logic
        break;

    case SDLK_UP:
        m_inputState.keyboardUpHeld = true;
        // Let App handle edge-turn and nudge logic
        break;

    case SDLK_DOWN:
        m_inputState.keyboardDownHeld = true;
        // Let App handle edge-turn and nudge logic
        break;

    case SDLK_PAGEDOWN:
        if (!isInPageChangeCooldown())
        {
            actionData.action = InputAction::GoToNextPage;
        }
        break;

    case SDLK_PAGEUP:
        if (!isInPageChangeCooldown())
        {
            actionData.action = InputAction::GoToPreviousPage;
        }
        break;

    case SDLK_PLUS:
    case SDLK_KP_PLUS:
        actionData.action = InputAction::ZoomIn;
        actionData.intValue = m_zoomStep;
        break;

    case SDLK_MINUS:
    case SDLK_KP_MINUS:
        actionData.action = InputAction::ZoomOut;
        actionData.intValue = m_zoomStep;
        break;

    case SDLK_HOME:
        actionData.action = InputAction::GoToFirstPage;
        break;

    case SDLK_END:
        actionData.action = InputAction::GoToLastPage;
        break;

    case SDLK_0:
    case SDLK_KP_0:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::HandlePageJumpInput;
            actionData.charValue = '0';
        }
        else
        {
            actionData.action = InputAction::ZoomTo;
            actionData.intValue = 100;
        }
        break;

    case SDLK_1:
    case SDLK_KP_1:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::HandlePageJumpInput;
            actionData.charValue = '1';
        }
        break;

    case SDLK_2:
    case SDLK_KP_2:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::HandlePageJumpInput;
            actionData.charValue = '2';
        }
        break;

    case SDLK_3:
    case SDLK_KP_3:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::HandlePageJumpInput;
            actionData.charValue = '3';
        }
        break;

    case SDLK_4:
    case SDLK_KP_4:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::HandlePageJumpInput;
            actionData.charValue = '4';
        }
        break;

    case SDLK_5:
    case SDLK_KP_5:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::HandlePageJumpInput;
            actionData.charValue = '5';
        }
        break;

    case SDLK_6:
    case SDLK_KP_6:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::HandlePageJumpInput;
            actionData.charValue = '6';
        }
        break;

    case SDLK_7:
    case SDLK_KP_7:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::HandlePageJumpInput;
            actionData.charValue = '7';
        }
        break;

    case SDLK_8:
    case SDLK_KP_8:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::HandlePageJumpInput;
            actionData.charValue = '8';
        }
        break;

    case SDLK_9:
    case SDLK_KP_9:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::HandlePageJumpInput;
            actionData.charValue = '9';
        }
        break;

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (m_pageJumpActive)
        {
            actionData.action = InputAction::ConfirmPageJumpInput;
        }
        break;

    case SDLK_f:
        actionData.action = InputAction::ToggleFullscreen;
        break;

    case SDLK_g:
        actionData.action = InputAction::StartPageJumpInput;
        break;

    case SDLK_m:
        actionData.action = InputAction::ToggleFontMenu;
        break;

    case SDLK_p:
        actionData.action = InputAction::PrintAppState;
        break;

    case SDLK_c:
        actionData.action = InputAction::ClampScroll;
        break;

    case SDLK_w:
        actionData.action = InputAction::FitPageToWidth;
        break;

    case SDLK_r:
        if (SDL_GetModState() & KMOD_SHIFT)
        {
            actionData.action = InputAction::RotateClockwise;
        }
        else
        {
            actionData.action = InputAction::ResetPageView;
        }
        break;

    case SDLK_h:
        actionData.action = InputAction::ToggleMirrorHorizontal;
        break;

    case SDLK_v:
        actionData.action = InputAction::ToggleMirrorVertical;
        break;

    case SDLK_LEFTBRACKET:
        if (!isInPageChangeCooldown())
        {
            actionData.action = InputAction::JumpPages;
            actionData.intValue = -10;
        }
        break;

    case SDLK_RIGHTBRACKET:
        if (!isInPageChangeCooldown())
        {
            actionData.action = InputAction::JumpPages;
            actionData.intValue = 10;
        }
        break;
    }

    return actionData;
}

InputActionData InputManager::processKeyUp(const SDL_Event& event)
{
    InputActionData actionData;

    switch (event.key.keysym.sym)
    {
    case SDLK_RIGHT:
        m_inputState.keyboardRightHeld = false;
        if (m_inputState.edgeTurnHoldRight > 0.0f)
        {
            m_inputState.edgeTurnCooldownRight = SDL_GetTicks() / 1000.0f;
        }
        m_inputState.edgeTurnHoldRight = 0.0f;
        break;

    case SDLK_LEFT:
        m_inputState.keyboardLeftHeld = false;
        if (m_inputState.edgeTurnHoldLeft > 0.0f)
        {
            m_inputState.edgeTurnCooldownLeft = SDL_GetTicks() / 1000.0f;
        }
        m_inputState.edgeTurnHoldLeft = 0.0f;
        break;

    case SDLK_UP:
        m_inputState.keyboardUpHeld = false;
        if (m_inputState.edgeTurnHoldUp > 0.0f)
        {
            m_inputState.edgeTurnCooldownUp = SDL_GetTicks() / 1000.0f;
        }
        m_inputState.edgeTurnHoldUp = 0.0f;
        break;

    case SDLK_DOWN:
        m_inputState.keyboardDownHeld = false;
        if (m_inputState.edgeTurnHoldDown > 0.0f)
        {
            m_inputState.edgeTurnCooldownDown = SDL_GetTicks() / 1000.0f;
        }
        m_inputState.edgeTurnHoldDown = 0.0f;
        break;
    }

    return actionData;
}

InputActionData InputManager::processControllerButton(const SDL_Event& event)
{
    InputActionData actionData;

    if (event.cbutton.which != m_gameControllerInstanceID)
    {
        return actionData;
    }

    SDL_GameControllerButton physicalButton = static_cast<SDL_GameControllerButton>(event.cbutton.button);

    if (event.type == SDL_CONTROLLERBUTTONDOWN)
    {
        if (physicalButton == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        {
            m_leftShoulderHeld = true;
        }
        else if (physicalButton == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
        {
            m_rightShoulderHeld = true;
        }

        LogicalButton logicalButton = m_buttonMapper.mapButton(physicalButton);

        switch (logicalButton)
        {
        case LogicalButton::DPadRight:
            m_inputState.dpadRightHeld = true;
            m_dpadRightButtonDown = true;
            break;

        case LogicalButton::DPadLeft:
            m_inputState.dpadLeftHeld = true;
            m_dpadLeftButtonDown = true;
            break;

        case LogicalButton::DPadUp:
            m_inputState.dpadUpHeld = true;
            m_dpadUpButtonDown = true;
            break;

        case LogicalButton::DPadDown:
            m_inputState.dpadDownHeld = true;
            m_dpadDownButtonDown = true;
            break;

        case LogicalButton::PagePrevious:
            if (!isInPageChangeCooldown())
            {
                actionData.action = InputAction::GoToPreviousPage;
            }
            break;

        case LogicalButton::PageNext:
            if (!isInPageChangeCooldown())
            {
                actionData.action = InputAction::GoToNextPage;
            }
            break;

        case LogicalButton::Special:
            actionData.action = InputAction::ZoomIn;
            actionData.intValue = m_zoomStep;
            break;

        case LogicalButton::Cancel:
            actionData.action = InputAction::ZoomOut;
            actionData.intValue = m_zoomStep;
            break;

        case LogicalButton::Alternate:
            actionData.action = InputAction::RotateClockwise;
            break;

        case LogicalButton::Accept:
            actionData.action = InputAction::FitPageToWidth;
            break;

        case LogicalButton::Quit:
            actionData.action = InputAction::Quit;
            break;

        case LogicalButton::Menu:
            actionData.action = InputAction::ToggleFontMenu;
            break;

        case LogicalButton::MirrorHorizontal:
            std::cout << "[InputManager] MirrorHorizontal action triggered!" << std::endl;
            actionData.action = InputAction::ToggleMirrorHorizontal;
            break;

        case LogicalButton::Options:
            actionData.action = InputAction::ToggleMirrorVertical;
            break;

        case LogicalButton::Extra1:
        case LogicalButton::Extra2:
            // These are handled via SDL_JOYBUTTONDOWN on TG5040
            // Should be filtered out earlier in processEvent()
            break;
        }

        if (shouldTriggerCombo(m_leftShoulderHeld, m_rightShoulderHeld, m_shoulderComboLatched))
        {
            actionData.action = InputAction::ToggleFontMenu;
        }
    }
    else if (event.type == SDL_CONTROLLERBUTTONUP)
    {
        LogicalButton logicalButton = m_buttonMapper.mapButton(physicalButton);

        if (physicalButton == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        {
            m_leftShoulderHeld = false;
            m_shoulderComboLatched = false;
        }
        else if (physicalButton == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
        {
            m_rightShoulderHeld = false;
            m_shoulderComboLatched = false;
        }

        switch (logicalButton)
        {
        case LogicalButton::DPadRight:
            m_dpadRightButtonDown = false;
            m_inputState.dpadRightHeld = false;
            if (m_inputState.edgeTurnHoldRight > 0.0f)
            {
                m_inputState.edgeTurnCooldownRight = SDL_GetTicks() / 1000.0f;
            }
            m_inputState.edgeTurnHoldRight = 0.0f;
            break;

        case LogicalButton::DPadLeft:
            m_dpadLeftButtonDown = false;
            m_inputState.dpadLeftHeld = false;
            if (m_inputState.edgeTurnHoldLeft > 0.0f)
            {
                m_inputState.edgeTurnCooldownLeft = SDL_GetTicks() / 1000.0f;
            }
            m_inputState.edgeTurnHoldLeft = 0.0f;
            break;

        case LogicalButton::DPadUp:
            m_dpadUpButtonDown = false;
            m_inputState.dpadUpHeld = false;
            if (m_inputState.edgeTurnHoldUp > 0.0f)
            {
                m_inputState.edgeTurnCooldownUp = SDL_GetTicks() / 1000.0f;
            }
            m_inputState.edgeTurnHoldUp = 0.0f;
            break;

        case LogicalButton::DPadDown:
            m_dpadDownButtonDown = false;
            m_inputState.dpadDownHeld = false;
            if (m_inputState.edgeTurnHoldDown > 0.0f)
            {
                m_inputState.edgeTurnCooldownDown = SDL_GetTicks() / 1000.0f;
            }
            m_inputState.edgeTurnHoldDown = 0.0f;
            break;

        default:
            // No action needed for button up on other buttons
            break;
        }
    }

    return actionData;
}

InputActionData InputManager::processControllerAxis(const SDL_Event& event)
{
    InputActionData actionData;

    if (event.caxis.which != m_gameControllerInstanceID)
    {
        return actionData;
    }

    const Sint16 AXIS_DEAD_ZONE = 8000;
    Uint32 now = SDL_GetTicks();

    switch (event.caxis.axis)
    {
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        m_leftTriggerActive = event.caxis.value > TRIGGER_DEAD_ZONE;
        if (m_leftTriggerActive)
        {
            m_leftTriggerActivatedAt = now;
        }
        if (m_leftTriggerActive && !isInPageChangeCooldown())
        {
            actionData.action = InputAction::JumpPages;
            actionData.intValue = -10;
        }
        if (shouldTriggerCombo(m_leftTriggerActive || (now - m_leftTriggerActivatedAt) < TRIGGER_COMBO_GRACE_MS,
                               m_rightTriggerActive || (now - m_rightTriggerActivatedAt) < TRIGGER_COMBO_GRACE_MS,
                               m_triggerComboLatched) &&
            !isInPageChangeCooldown())
        {
            actionData.action = InputAction::ResetPageView;
        }
        break;

    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        m_rightTriggerActive = event.caxis.value > TRIGGER_DEAD_ZONE;
        if (m_rightTriggerActive)
        {
            m_rightTriggerActivatedAt = now;
        }
        if (m_rightTriggerActive && !isInPageChangeCooldown())
        {
            actionData.action = InputAction::JumpPages;
            actionData.intValue = 10;
        }
        if (shouldTriggerCombo(m_leftTriggerActive || (now - m_leftTriggerActivatedAt) < TRIGGER_COMBO_GRACE_MS,
                               m_rightTriggerActive || (now - m_rightTriggerActivatedAt) < TRIGGER_COMBO_GRACE_MS,
                               m_triggerComboLatched) &&
            !isInPageChangeCooldown())
        {
            actionData.action = InputAction::ResetPageView;
        }
        break;

    case SDL_CONTROLLER_AXIS_LEFTX:
        m_leftStickX = event.caxis.value;
        updateDpadFromAnalog(m_leftStickX > AXIS_DEAD_ZONE, m_leftStickX < -AXIS_DEAD_ZONE,
                             m_leftStickY < -AXIS_DEAD_ZONE, m_leftStickY > AXIS_DEAD_ZONE);
        break;
    case SDL_CONTROLLER_AXIS_LEFTY:
        m_leftStickY = event.caxis.value;
        updateDpadFromAnalog(m_leftStickX > AXIS_DEAD_ZONE, m_leftStickX < -AXIS_DEAD_ZONE,
                             m_leftStickY < -AXIS_DEAD_ZONE, m_leftStickY > AXIS_DEAD_ZONE);
        break;

    case SDL_CONTROLLER_AXIS_RIGHTX:
        if (!isInScrollTimeout())
        {
            if (event.caxis.value < -AXIS_DEAD_ZONE)
            {
                actionData.action = InputAction::MoveRight;
                actionData.intValue = 20;
            }
            else if (event.caxis.value > AXIS_DEAD_ZONE)
            {
                actionData.action = InputAction::MoveLeft;
                actionData.intValue = 20;
            }
        }
        break;

    case SDL_CONTROLLER_AXIS_RIGHTY:
        if (!isInScrollTimeout())
        {
            if (event.caxis.value < -AXIS_DEAD_ZONE)
            {
                actionData.action = InputAction::MoveUp;
                actionData.intValue = 20;
            }
            else if (event.caxis.value > AXIS_DEAD_ZONE)
            {
                actionData.action = InputAction::MoveDown;
                actionData.intValue = 20;
            }
        }
        break;
    }

    return actionData;
}

bool InputManager::shouldTriggerCombo(bool leftActive, bool rightActive, bool& comboLatched)
{
    if (leftActive && rightActive)
    {
        if (!comboLatched)
        {
            comboLatched = true;
            return true;
        }
    }
    else
    {
        comboLatched = false;
    }

    return false;
}

void InputManager::updateDpadFromAnalog(bool rightActive, bool leftActive, bool upActive, bool downActive)
{
    auto updateDirection = [](bool desired, bool& state, float& hold, float& cooldown, bool buttonDown)
    {
        if (desired && !state)
        {
            state = true;
        }
        else if (!desired && state)
        {
            // Don't let analog stick clear held state if a D-pad button is physically pressed
            if (buttonDown) return;
            state = false;
            if (hold > 0.0f)
            {
                cooldown = SDL_GetTicks() / 1000.0f;
            }
            hold = 0.0f;
        }
    };

    updateDirection(rightActive, m_inputState.dpadRightHeld, m_inputState.edgeTurnHoldRight, m_inputState.edgeTurnCooldownRight, m_dpadRightButtonDown);
    updateDirection(leftActive, m_inputState.dpadLeftHeld, m_inputState.edgeTurnHoldLeft, m_inputState.edgeTurnCooldownLeft, m_dpadLeftButtonDown);
    updateDirection(upActive, m_inputState.dpadUpHeld, m_inputState.edgeTurnHoldUp, m_inputState.edgeTurnCooldownUp, m_dpadUpButtonDown);
    updateDirection(downActive, m_inputState.dpadDownHeld, m_inputState.edgeTurnHoldDown, m_inputState.edgeTurnCooldownDown, m_dpadDownButtonDown);
}

InputActionData InputManager::processMouse(const SDL_Event& event)
{
    InputActionData actionData;

    switch (event.type)
    {
    case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            m_inputState.isDragging = true;
            m_inputState.lastTouchX = static_cast<float>(event.button.x);
            m_inputState.lastTouchY = static_cast<float>(event.button.y);
            actionData.action = InputAction::StartDragging;
            actionData.floatValue = m_inputState.lastTouchX;
            actionData.deltaY = m_inputState.lastTouchY;
        }
        break;

    case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            m_inputState.isDragging = false;
            actionData.action = InputAction::StopDragging;
        }
        break;

    case SDL_MOUSEMOTION:
        if (m_inputState.isDragging && !isInScrollTimeout())
        {
            float dx = static_cast<float>(event.motion.x) - m_inputState.lastTouchX;
            float dy = static_cast<float>(event.motion.y) - m_inputState.lastTouchY;
            m_inputState.lastTouchX = static_cast<float>(event.motion.x);
            m_inputState.lastTouchY = static_cast<float>(event.motion.y);

            actionData.action = InputAction::UpdateDragging;
            actionData.deltaX = dx;
            actionData.deltaY = dy;
        }
        break;
    }

    return actionData;
}

InputActionData InputManager::updateHeldInputs(float dt)
{
    InputActionData actionData;

    // Update edge turn timers
    updateEdgeTurnTimers(dt);

    // Check for edge turn trigger
    if (checkEdgeTurnTrigger())
    {
        // Determine which direction triggered
        if (m_inputState.edgeTurnHoldRight >= EDGE_TURN_THRESHOLD)
        {
            actionData.action = InputAction::GoToNextPage;
            m_inputState.edgeTurnHoldRight = 0.0f;
            m_inputState.edgeTurnCooldownRight = SDL_GetTicks() / 1000.0f;
        }
        else if (m_inputState.edgeTurnHoldLeft >= EDGE_TURN_THRESHOLD)
        {
            actionData.action = InputAction::GoToPreviousPage;
            m_inputState.edgeTurnHoldLeft = 0.0f;
            m_inputState.edgeTurnCooldownLeft = SDL_GetTicks() / 1000.0f;
        }
        else if (m_inputState.edgeTurnHoldDown >= EDGE_TURN_THRESHOLD)
        {
            actionData.action = InputAction::GoToNextPage;
            m_inputState.edgeTurnHoldDown = 0.0f;
            m_inputState.edgeTurnCooldownDown = SDL_GetTicks() / 1000.0f;
        }
        else if (m_inputState.edgeTurnHoldUp >= EDGE_TURN_THRESHOLD)
        {
            actionData.action = InputAction::GoToPreviousPage;
            m_inputState.edgeTurnHoldUp = 0.0f;
            m_inputState.edgeTurnCooldownUp = SDL_GetTicks() / 1000.0f;
        }
    }

    return actionData;
}

void InputManager::updateEdgeTurnTimers(float dt)
{
    float currentTime = SDL_GetTicks() / 1000.0f;

    // Only update timers if input is held and not in cooldown
    if ((m_inputState.dpadRightHeld || m_inputState.keyboardRightHeld) &&
        (currentTime - m_inputState.edgeTurnCooldownRight) > EDGE_TURN_COOLDOWN_TIME)
    {
        m_inputState.edgeTurnHoldRight += dt;
    }

    if ((m_inputState.dpadLeftHeld || m_inputState.keyboardLeftHeld) &&
        (currentTime - m_inputState.edgeTurnCooldownLeft) > EDGE_TURN_COOLDOWN_TIME)
    {
        m_inputState.edgeTurnHoldLeft += dt;
    }

    if ((m_inputState.dpadUpHeld || m_inputState.keyboardUpHeld) &&
        (currentTime - m_inputState.edgeTurnCooldownUp) > EDGE_TURN_COOLDOWN_TIME)
    {
        m_inputState.edgeTurnHoldUp += dt;
    }

    if ((m_inputState.dpadDownHeld || m_inputState.keyboardDownHeld) &&
        (currentTime - m_inputState.edgeTurnCooldownDown) > EDGE_TURN_COOLDOWN_TIME)
    {
        m_inputState.edgeTurnHoldDown += dt;
    }
}

bool InputManager::checkEdgeTurnTrigger()
{
    return (m_inputState.edgeTurnHoldRight >= EDGE_TURN_THRESHOLD ||
            m_inputState.edgeTurnHoldLeft >= EDGE_TURN_THRESHOLD ||
            m_inputState.edgeTurnHoldUp >= EDGE_TURN_THRESHOLD ||
            m_inputState.edgeTurnHoldDown >= EDGE_TURN_THRESHOLD);
}

bool InputManager::isInPageChangeCooldown() const
{
    return (SDL_GetTicks() - m_lastPageChangeTime) < PAGE_CHANGE_COOLDOWN_MS;
}

bool InputManager::isInScrollTimeout() const
{
    return (SDL_GetTicks() - m_lastScrollTime) < SCROLL_TIMEOUT_MS;
}

void InputManager::setPageChangeCooldown()
{
    m_lastPageChangeTime = SDL_GetTicks();
}

void InputManager::setScrollTimeout()
{
    m_lastScrollTime = SDL_GetTicks();
}

void InputManager::initializeGameControllers()
{
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        if (SDL_IsGameController(i))
        {
            m_gameController = SDL_GameControllerOpen(i);
            if (m_gameController)
            {
                m_gameControllerInstanceID = SDL_JoystickGetDeviceInstanceID(i);
                std::cout << "Opened game controller: " << SDL_GameControllerName(m_gameController) << std::endl;
                break; // Only open the first controller
            }
            else
            {
                std::cerr << "Could not open game controller " << i << ": " << SDL_GetError() << std::endl;
            }
        }
    }
}

void InputManager::closeGameControllers()
{
    if (m_gameController)
    {
        SDL_GameControllerClose(m_gameController);
        m_gameController = nullptr;
        m_gameControllerInstanceID = -1;
        std::cout << "Game controller closed." << std::endl;
    }
}

void InputManager::handlePageJumpInput(char c)
{
    if (m_pageJumpBuffer.length() < 10)
    { // Prevent overflow
        m_pageJumpBuffer += c;
    }
}
