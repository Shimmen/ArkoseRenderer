#pragma once

#include <mooslib/vector.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

class Input final {
public:
    Input(Input& other) = delete;
    Input(Input&& other) = delete;
    Input& operator=(Input& other) = delete;

    static const Input& instance();
    static void registerWindow(GLFWwindow*);
    static void preEventPoll();

    static void keyEventCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonEventCallback(GLFWwindow* window, int button, int action, int mods);
    static void mouseMovementEventCallback(GLFWwindow* window, double xPos, double yPos);
    static void mouseScrollEventCallback(GLFWwindow* window, double xOffset, double yOffset);

    [[nodiscard]] bool isKeyDown(int key) const;
    [[nodiscard]] bool wasKeyPressed(int key) const;
    [[nodiscard]] bool wasKeyReleased(int key) const;

    [[nodiscard]] bool isButtonDown(int button) const;
    [[nodiscard]] bool wasButtonPressed(int button) const;
    [[nodiscard]] bool wasButtonReleased(int button) const;

    [[nodiscard]] vec2 mousePosition(GLFWwindow* window) const;
    [[nodiscard]] vec2 mouseDelta() const;
    [[nodiscard]] float scrollDelta() const;

    [[nodiscard]] vec2 leftStick() const;
    [[nodiscard]] vec2 rightStick() const;

private:
    Input() = default;
    ~Input() = default;

    static Input s_instance;

    static constexpr int KEYBOARD_KEY_COUNT { GLFW_KEY_LAST };
    static constexpr int MOUSE_BUTTON_COUNT { GLFW_MOUSE_BUTTON_LAST };

    static constexpr float GAMEPAD_DEADZONE { 0.25f };

    bool m_isKeyDown[KEYBOARD_KEY_COUNT] {};
    bool m_wasKeyPressed[KEYBOARD_KEY_COUNT] {};
    bool m_wasKeyReleased[KEYBOARD_KEY_COUNT] {};

    bool m_isButtonDown[MOUSE_BUTTON_COUNT] {};
    bool m_wasButtonPressed[MOUSE_BUTTON_COUNT] {};
    bool m_wasButtonReleased[MOUSE_BUTTON_COUNT] {};

    double m_currentXPosition { 0.0 };
    double m_currentYPosition { 0.0 };
    double m_lastXPosition { -1.0 };
    double m_lastYPosition { -1.0 };

    double m_currentScollOffset { 0.0 };
    double m_lastScrollOffset { 0.0 };
};
