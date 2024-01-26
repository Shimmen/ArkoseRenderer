#pragma once

#include "system/System.h"

struct GLFWwindow;

class SystemGlfw : public System {
public:
    explicit SystemGlfw();
    ~SystemGlfw() override;

    ARK_NON_COPYABLE(SystemGlfw)

    bool createWindow(WindowType, Extent2D const& windowSize) override;

    Extent2D windowSize() const override;
    Extent2D windowFramebufferSize() const override;
    bool windowIsFullscreen() override;

    bool newFrame() override;
    bool exitRequested() override;
    void waitEvents() override;

    bool canProvideMousePosition() const override { return true; }
    vec2 currentMousePosition() const override;

    double timeSinceStartup() override;

#if defined(PLATFORM_WINDOWS)
    HWND win32WindowHandle() override;
#endif

#if defined(WITH_VULKAN)
    virtual char const** requiredInstanceExtensions(u32* count) override;
    virtual void* createVulkanSurface(void*) override;
#endif

private:
    // glfw event callbacks
    static void keyEventCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonEventCallback(GLFWwindow* window, int button, int action, int mods);
    static void mouseMovementEventCallback(GLFWwindow* window, double xPos, double yPos);
    static void mouseScrollEventCallback(GLFWwindow* window, double xOffset, double yOffset);

private:
    GLFWwindow* m_glfwWindow { nullptr };
    Extent2D m_lastWindowSize { 0, 0 };
};
