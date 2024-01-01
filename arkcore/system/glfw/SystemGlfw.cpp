#include "SystemGlfw.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include "system/Input.h"

#if defined(WITH_VULKAN)
#include <vulkan/vulkan.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(PLATFORM_WINDOWS)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

SystemGlfw::SystemGlfw()
{
    SCOPED_PROFILE_ZONE();

    if (!glfwInit()) {
        ARKOSE_LOG(Fatal, "SystemGlfw: could not initialize glfw, exiting.");
    }
}

SystemGlfw::~SystemGlfw()
{
    if (m_glfwWindow != nullptr) {
        glfwDestroyWindow(m_glfwWindow);
    }

    glfwTerminate();
}

bool SystemGlfw::createWindow(WindowType windowType, Extent2D const& windowSize)
{
    SCOPED_PROFILE_ZONE();

    // NOTE: This is valid as long as we don't want an OpenGL or OpenGLES context (we support neither)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    std::string windowTitle = "Arkose";

    switch (windowType) {
    case WindowType::Fullscreen: {
        GLFWmonitor* defaultMonitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* defaultVideoMode = glfwGetVideoMode(defaultMonitor);
        m_glfwWindow = glfwCreateWindow(defaultVideoMode->width, defaultVideoMode->height, windowTitle.c_str(), defaultMonitor, nullptr);
        break;
    }
    case WindowType::Windowed: {
        m_glfwWindow = glfwCreateWindow(windowSize.width(), windowSize.height(), windowTitle.c_str(), nullptr, nullptr);
        break;
    }
    }

    if (!m_glfwWindow) {
        ARKOSE_LOG(Fatal, "SystemGlfw: could not create window with specified settings, exiting.");
    }

    Input::registerWindow(m_glfwWindow);

    return true;
}

Extent2D SystemGlfw::windowSize() const
{
    int windowWidthPx, windowHeightPx;
    glfwGetWindowSize(m_glfwWindow, &windowWidthPx, &windowHeightPx);
    return Extent2D(windowWidthPx, windowHeightPx);
}

Extent2D SystemGlfw::windowFramebufferSize() const
{
    int width, height;
    glfwGetFramebufferSize(m_glfwWindow, &width, &height);
    return Extent2D(width, height);
}

bool SystemGlfw::windowIsFullscreen()
{
    GLFWmonitor* glfwMonitor = glfwGetWindowMonitor(m_glfwWindow);
    return glfwMonitor != nullptr;
}

bool SystemGlfw::exitRequested()
{
    return static_cast<bool>(glfwWindowShouldClose(m_glfwWindow));
}

void SystemGlfw::pollEvents()
{
    // NOTE: This can only be called once a frame - should be done from the main loop!

    Input::preEventPoll();
    glfwPollEvents();
}

void SystemGlfw::waitEvents()
{
    glfwWaitEvents();
}

double SystemGlfw::timeSinceStartup()
{
    return glfwGetTime();
}

#if defined(PLATFORM_WINDOWS)
HWND SystemGlfw::win32WindowHandle()
{
    return glfwGetWin32Window(m_glfwWindow);
}
#endif

#if defined(WITH_VULKAN)
char const** SystemGlfw::requiredInstanceExtensions(u32* count)
{
    return glfwGetRequiredInstanceExtensions(count);
}
void* SystemGlfw::createVulkanSurface(void* vulkanInstanceUntyped)
{
    VkInstance vulkanInstance = reinterpret_cast<VkInstance>(vulkanInstanceUntyped);

    if (!glfwVulkanSupported()) {
        char const* glfwErrorMessage;
        int glfwErrorCode = glfwGetError(&glfwErrorMessage);
        ARKOSE_ASSERT(glfwErrorCode != GLFW_NO_ERROR);
        ARKOSE_LOG(Fatal, "SystemGlfw: Vulkan is not supported. Reason: {}. Exiting.", glfwErrorMessage);
    }

    VkSurfaceKHR vulkanSurface;
    if (glfwCreateWindowSurface(vulkanInstance, m_glfwWindow, nullptr, &vulkanSurface) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "SystemGlfw: can't create Vulkan window surface, exiting.");
    }

    return vulkanSurface;
}
#endif
