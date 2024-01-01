#pragma once

#include "utility/Extent.h"
#include <ark/copying.h>

class System {
public:
    static bool initialize();
    static void shutdown();

    static System& get();

    ARK_NON_COPYABLE(System)

    ////////////////////////////////////////////////////////////////////////////
    // Generic system API

    enum class WindowType {
        Windowed,
        Fullscreen
    };

    virtual bool createWindow(WindowType windowType, Extent2D const& windowSize) = 0;

    virtual Extent2D windowSize() const = 0;
    virtual Extent2D windowFramebufferSize() const = 0;
    virtual bool windowIsFullscreen() = 0;

    virtual void newFrame() = 0;
    virtual bool exitRequested() = 0;
    virtual void waitEvents() = 0;

    virtual bool canProvideMousePosition() const { return false; }
    virtual vec2 currentMousePosition() const { return vec2(0.0f); }

    virtual double timeSinceStartup() = 0;

    System() = default;
    virtual ~System() { }

    ////////////////////////////////////////////////////////////////////////////
    // Platform- & graphics backend specific hacky stuff

#if defined(PLATFORM_WINDOWS)
    virtual HWND win32WindowHandle() = 0;
#endif

#if defined(WITH_VULKAN)
    // Certain systems may need specific vulkan instance extensions to work. Call this function
    // to get a list of these extensions before creating the vulkan instance.
    virtual char const** requiredInstanceExtensions(u32* count) = 0;

    // NOTE: Vulkan instance parameter must be of type `VkInstance`, return value will be of type `VkSurfaceKHR`
    // We just don't want to include vulkan headers in System.h to avoid trashing of the global namespace..
    virtual void* createVulkanSurface(void* vulkanInstance) = 0;
#endif

private:
    static System* s_system;
};
