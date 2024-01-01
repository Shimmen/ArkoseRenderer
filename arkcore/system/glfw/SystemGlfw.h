#pragma once

#include "system/System.h"

struct GLFWwindow;

class SystemGlfw : public System {
public:
    explicit SystemGlfw();
    ~SystemGlfw() override;

    bool createWindow(WindowType, Extent2D const& windowSize) override;

    Extent2D windowSize() const override;
    Extent2D windowFramebufferSize() const override;
    bool windowIsFullscreen() override;

    bool exitRequested() override;
    void pollEvents() override;
    void waitEvents() override;

    double timeSinceStartup() override;

#if defined(PLATFORM_WINDOWS)
    HWND win32WindowHandle() override;
#endif

#if defined(WITH_VULKAN)
    virtual char const** requiredInstanceExtensions(u32* count) override;
    virtual void* createVulkanSurface(void*) override;
#endif

    // ugly hack.. needed for the Vulkan backend, for now.
    GLFWwindow* glfwWindowHack() const { return m_glfwWindow; }

private:
    GLFWwindow* m_glfwWindow { nullptr };
};
