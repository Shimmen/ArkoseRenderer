#include "Input.h"

#include "core/Assert.h"
#include "system/System.h"
#include <imgui.h>

Input Input::s_instance {};

Input const& Input::instance()
{
    return s_instance;
}

Input& Input::mutableInstance()
{
    return s_instance;
}

bool Input::isKeyDown(Key key) const
{
    int val = static_cast<int>(key);
    return m_isKeyDown[val];
}

bool Input::wasKeyPressed(Key key) const
{
    int val = static_cast<int>(key);
    return m_wasKeyPressed[val];
}

bool Input::wasKeyReleased(Key key) const
{
    int val = static_cast<int>(key);
    return m_wasKeyPressed[val];
}

bool Input::isButtonDown(Button button) const
{
    int val = static_cast<int>(button);
    return m_isButtonDown[val];
}

bool Input::wasButtonPressed(Button button) const
{
    int val = static_cast<int>(button);
    return m_wasButtonPressed[val];
}

bool Input::wasButtonReleased(Button button) const
{
    int val = static_cast<int>(button);
    return m_wasButtonReleased[val];
}

bool Input::didClickButton(Button button) const
{
    int val = static_cast<int>(button);
    return m_wasButtonClicked[val];
}

vec2 Input::mousePosition() const
{
    ARKOSE_ASSERT(System::get().canProvideMousePosition());
    return System::get().currentMousePosition();
}

vec2 Input::mouseDelta() const
{
    float dx = (float)(m_currentXPosition - m_lastXPosition);
    float dy = (float)(m_currentYPosition - m_lastYPosition);
    return vec2(dx, dy);
}

float Input::scrollDelta() const
{
    return float(m_currentScollOffset - m_lastScrollOffset);
}

bool Input::isGuiUsingMouse() const
{
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse;
}

bool Input::isGuiUsingKeyboard() const
{
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

GamepadState const& Input::gamepadState(GamepadId gamepadId) const
{
    u32 gamepadIndex = static_cast<u32>(gamepadId);
    if (m_gamepadActive[gamepadIndex]) {
        return m_gamepadState[gamepadIndex];
    } else {
        return m_nullGamepadState;
    }
}

vec2 Input::leftStick(GamepadId gamepadId) const
{
    u32 gamepadIndex = static_cast<u32>(gamepadId);
    if (m_gamepadActive[gamepadIndex]) {
        return normalizeGamepadStick(m_gamepadState[gamepadIndex].leftStick());
    }
    return { 0, 0 };
}

vec2 Input::rightStick(GamepadId gamepadId) const
{
    u32 gamepadIndex = static_cast<u32>(gamepadId);
    if (m_gamepadActive[gamepadIndex]) {
        return normalizeGamepadStick(m_gamepadState[gamepadIndex].rightStick());
    }
    return { 0, 0 };
}

void Input::preEventPoll()
{
    memset(m_wasKeyPressed, false, KeyboardKeyCount * sizeof(bool));
    memset(m_wasKeyReleased, false, KeyboardKeyCount * sizeof(bool));

    memset(m_wasButtonPressed, false, MouseButtonCount * sizeof(bool));
    memset(m_wasButtonReleased, false, MouseButtonCount * sizeof(bool));
    memset(m_wasButtonClicked, false, MouseButtonCount * sizeof(bool));

    m_lastXPosition = m_currentXPosition;
    m_lastYPosition = m_currentYPosition;
    m_lastScrollOffset = m_currentScollOffset;
}

void Input::keyEventCallback(int key, int scancode, InputAction action, InputModifiers mods)
{
    switch (action) {
    case InputAction::Press:
        m_wasKeyPressed[key] = true;
        m_isKeyDown[key] = true;
        break;

    case InputAction::Release:
        m_wasKeyReleased[key] = true;
        m_isKeyDown[key] = false;
        break;

    case InputAction::Repeat:
        // TODO: Handle repeat events!
    default:
        break;
    }
}

void Input::mouseButtonEventCallback(int button, InputAction action, InputModifiers mods)
{
    vec2 mousePos = mousePosition();

    switch (action) {
    case InputAction::Press:
        m_wasButtonPressed[button] = true;
        m_isButtonDown[button] = true;
        m_buttonPressMousePosition[button] = mousePos;
        break;

    case InputAction::Release:
        m_wasButtonReleased[button] = true;
        m_isButtonDown[button] = false;

        if (m_buttonPressMousePosition[button].has_value()) {
            if (ark::distance(m_buttonPressMousePosition[button].value(), mousePos) <= MouseClickMaxAllowedDelta) {
                m_wasButtonClicked[button] = true;
                m_buttonPressMousePosition[button].reset();
            }
        }

        break;

    case InputAction::Repeat:
        // TODO: Handle repeat events!
    default:
        break;
    }
}

void Input::mouseMovementEventCallback(double xPosition, double yPosition)
{
    m_currentXPosition = xPosition;
    m_currentYPosition = yPosition;

    if (m_lastXPosition == -1.0) {
        m_lastXPosition = xPosition;
        m_lastYPosition = yPosition;
    }
}

void Input::mouseScrollEventCallback(double xOffset, double yOffset)
{
    // Ignore x-offset for now...
    m_currentScollOffset += yOffset;
}

void Input::setGamepadState(u32 gamepadIdx, GamepadState const& gamepadState)
{
    if (gamepadIdx < MaxGamepadCount) {
        m_gamepadActive[gamepadIdx] = true;
        m_gamepadState[gamepadIdx] = gamepadState;
    }
}

void Input::setGamepadInactive(u32 gamepadIdx)
{
    if (gamepadIdx < MaxGamepadCount) {
        m_gamepadActive[gamepadIdx] = false;
    }
}

vec2 Input::normalizeGamepadStick(vec2 stickValue) const
{
    if (length(stickValue) < GamepadDeadzone) {
        return { 0, 0 };
    } else {
        return normalize(stickValue) * ((length(stickValue) - GamepadDeadzone) / (1.0f - GamepadDeadzone));
    }
}
