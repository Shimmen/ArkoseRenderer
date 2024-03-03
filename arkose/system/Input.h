#pragma once

#include "core/Types.h"
#include "utility/EnumHelpers.h"
#include "system/InputAction.h"
#include "system/Gamepad.h"
#include <ark/vector.h>
#include <array>
#include <optional>

// Values map directly to GLFW's key defines
enum class Key : int {
    Space = 32,
    Apostrophe = 39, /* ' */
    Comma = 44, /* , */
    Minus = 45, /* - */
    Period = 46, /* . */
    Slash = 47, /* / */
    Num0 = 48,
    Num1 = 49,
    Num2 = 50,
    Num3 = 51,
    Num4 = 52,
    Num5 = 53,
    Num6 = 54,
    Num7 = 55,
    Num8 = 56,
    Num9 = 57,
    Semicolon = 59, /* ; */
    Equal = 61, /* = */
    A = 65,
    B = 66,
    C = 67,
    D = 68,
    E = 69,
    F = 70,
    G = 71,
    H = 72,
    I = 73,
    J = 74,
    K = 75,
    L = 76,
    M = 77,
    N = 78,
    O = 79,
    P = 80,
    Q = 81,
    R = 82,
    S = 83,
    T = 84,
    U = 85,
    V = 86,
    W = 87,
    X = 88,
    Y = 89,
    Z = 90,
    LeftBracket = 91, /* [ */
    Backslash = 92, /* \ */
    RightBracket = 93, /* ] */
    GraveAccent = 96, /* ` */
    World1 = 161, /* non-US #1 */
    World2 = 162, /* non-US #2 */

    /* Function keys */
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    PageUp = 266,
    PageDown = 267,
    Home = 268,
    End = 269,
    CapsLock = 280,
    ScrollLock = 281,
    NumLock = 282,
    PrintScreen = 283,
    Pause = 284,
    F1 = 290,
    F2 = 291,
    F3 = 292,
    F4 = 293,
    F5 = 294,
    F6 = 295,
    F7 = 296,
    F8 = 297,
    F9 = 298,
    F10 = 299,
    F11 = 300,
    F12 = 301,
    F13 = 302,
    F14 = 303,
    F15 = 304,
    F16 = 305,
    F17 = 306,
    F18 = 307,
    F19 = 308,
    F20 = 309,
    F21 = 310,
    F22 = 311,
    F23 = 312,
    F24 = 313,
    F25 = 314,
    Kp0 = 320,
    Kp1 = 321,
    Kp2 = 322,
    Kp3 = 323,
    Kp4 = 324,
    Kp5 = 325,
    Kp6 = 326,
    Kp7 = 327,
    Kp8 = 328,
    Kp9 = 329,
    KpDecimal = 330,
    KpDivide = 331,
    KpMultiply = 332,
    KpSubtract = 333,
    KpAdd = 334,
    KpEnter = 335,
    KpEqual = 336,
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,
    Menu = 348,
    __Max = Menu,
    __Count = __Max + 1,
};

// Values map directly to GLFW's button defines
enum class Button : int {
    B1 = 0,
    B2 = 1,
    B3 = 2,
    B4 = 3,
    B5 = 4,
    B6 = 5,
    B7 = 6,
    B8 = 7,
    Left = B1,
    Right = B2,
    Middle = B3,
    __Max = B8,
    __Count = __Max + 1,
};

// Values map directly to GLFW's modifier defines
enum class InputModifier {
    Shift = 1,
    Control = 2,
    Alt = 4,
    Super = 8,
    CapsLock = 16,
    NumLock = 32,
};

ARKOSE_ENUM_CLASS_BIT_FLAGS(InputModifier)
using InputModifiers = InputModifier;

enum class GamepadId : u32 {
    Gamepad0 = 0,
    Gamepad1 = 1,
    Gamepad2 = 2,
    Gamepad3 = 3,
    __Max = Gamepad3,
    __Count = __Max + 1,
};
ARKOSE_ENUM_CLASS_BIT_FLAGS(GamepadId)

class Input final {
public:
    Input(Input& other) = delete;
    Input(Input&& other) = delete;
    Input& operator=(Input& other) = delete;

    static Input const& instance();
    static Input& mutableInstance(); // TODO: Make sure only System classes touch this!

    [[nodiscard]] bool isKeyDown(Key) const;
    [[nodiscard]] bool wasKeyPressed(Key) const;
    [[nodiscard]] bool wasKeyReleased(Key) const;

    [[nodiscard]] bool isButtonDown(Button) const;
    [[nodiscard]] bool wasButtonPressed(Button) const;
    [[nodiscard]] bool wasButtonReleased(Button) const;
    [[nodiscard]] bool didClickButton(Button) const;

    [[nodiscard]] vec2 mousePosition() const;
    [[nodiscard]] vec2 mouseDelta() const;
    [[nodiscard]] float scrollDelta() const;

    [[nodiscard]] bool isGuiUsingMouse() const;
    [[nodiscard]] bool isGuiUsingKeyboard() const;

    [[nodiscard]] GamepadState const& gamepadState(GamepadId) const;
    [[nodiscard]] vec2 leftStick(GamepadId) const;
    [[nodiscard]] vec2 rightStick(GamepadId) const;

    void preEventPoll();
    void keyEventCallback(int key, int scancode, InputAction, InputModifiers);
    void mouseButtonEventCallback(int button, InputAction, InputModifiers);
    void mouseMovementEventCallback(double xPosition, double yPosition);
    void mouseScrollEventCallback(double xOffset, double yOffset);

    void setGamepadState(u32 gamepadIdx, GamepadState const&);
    void setGamepadInactive(u32 gamepadIdx);

    vec2 normalizeGamepadStick(vec2 stickValue) const;

private:
    Input() = default;
    ~Input() = default;

    static Input s_instance;

    static constexpr int KeyboardKeyCount { static_cast<int>(Key::__Count) };
    static constexpr int MouseButtonCount { static_cast<int>(Button::__Count) };

    static constexpr float GamepadDeadzone { 0.25f };
    static constexpr float MouseClickMaxAllowedDelta { 4.0f };

    bool m_isKeyDown[KeyboardKeyCount] {};
    bool m_wasKeyPressed[KeyboardKeyCount] {};
    bool m_wasKeyReleased[KeyboardKeyCount] {};

    bool m_isButtonDown[MouseButtonCount] {};
    bool m_wasButtonPressed[MouseButtonCount] {};
    bool m_wasButtonReleased[MouseButtonCount] {};

    bool m_wasButtonClicked[MouseButtonCount] {};
    std::optional<vec2> m_buttonPressMousePosition[MouseButtonCount] {};

    double m_currentXPosition { 0.0 };
    double m_currentYPosition { 0.0 };
    double m_lastXPosition { -1.0 };
    double m_lastYPosition { -1.0 };

    double m_currentScollOffset { 0.0 };
    double m_lastScrollOffset { 0.0 };

    static constexpr u32 MaxGamepadCount = static_cast<u32>(GamepadId::__Count);
    std::array<GamepadState, MaxGamepadCount> m_gamepadState {};
    std::array<bool, MaxGamepadCount> m_gamepadActive { 0 };

    GamepadState m_nullGamepadState {};
};
