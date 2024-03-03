#pragma once

#include "system/InputAction.h"
#include <ark/vector.h>

class GamepadState {
public:
    GamepadState() = default;
    GamepadState(vec2 leftStick, vec2 rightStick, float leftTrigger, float rightTrigger)
        : m_leftStick(leftStick)
        , m_rightStick(rightStick)
        , m_leftTrigger(leftTrigger)
        , m_rightTrigger(rightTrigger)
    {
    }

    vec2 const& leftStick() const { return m_leftStick; }
    vec2 const& rightStick() const { return m_rightStick; }

    float leftTrigger() const { return m_leftTrigger; }
    float rightTrigger() const { return m_rightTrigger; }

private:
    vec2 m_leftStick { 0.0f, 0.0f };
    vec2 m_rightStick { 0.0f, 0.0f };

    float m_leftTrigger { 0.0f };
    float m_rightTrigger { 0.0f };

    // eh let's worry about persistent state - like buttons - later
    // InputAction ...
};
