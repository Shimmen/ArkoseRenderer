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

    // Set up input for the window
    glfwSetWindowUserPointer(m_glfwWindow, this);
    glfwSetKeyCallback(m_glfwWindow, SystemGlfw::keyEventCallback);
    glfwSetMouseButtonCallback(m_glfwWindow, SystemGlfw::mouseButtonEventCallback);
    glfwSetCursorPosCallback(m_glfwWindow, SystemGlfw::mouseMovementEventCallback);
    glfwSetScrollCallback(m_glfwWindow, SystemGlfw::mouseScrollEventCallback);

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

void SystemGlfw::newFrame()
{
    Input::mutableInstance().preEventPoll();
    glfwPollEvents(); // will trigger calls to the event callbacks immediately

    // glfw doesn't use callbacks for joysticks / gamepads, needs to be polled manually
    for (int joystick = GLFW_JOYSTICK_1; joystick < GLFW_JOYSTICK_LAST; ++joystick) {
        if (!glfwJoystickPresent(joystick)) {
            continue;
        }

        if (!glfwJoystickIsGamepad(joystick)) { 
            continue;
        }

        GLFWgamepadstate glfwGamepadState;
        if (glfwGetGamepadState(GLFW_JOYSTICK_1, &glfwGamepadState) != GLFW_FALSE) {
            // TODO: Add support for gamepads!
            //  1. remap the state to something non-glfw specific
            //  2. pass it to the Input object
            //  3. done?
        }
    }
}

bool SystemGlfw::exitRequested()
{
    return static_cast<bool>(glfwWindowShouldClose(m_glfwWindow));
}

void SystemGlfw::waitEvents()
{
    glfwWaitEvents();
}

vec2 SystemGlfw::currentMousePosition() const
{
    double x, y;
    glfwGetCursorPos(m_glfwWindow, &x, &y);
    return vec2(static_cast<float>(x), static_cast<float>(y));
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

static InputAction glfwActionToInputAction(int glfwAction)
{
    switch (glfwAction) {
    case GLFW_RELEASE:
        return InputAction::Release;
    case GLFW_PRESS:
        return InputAction::Press;
    case GLFW_REPEAT:
        return InputAction::Repeat;
    default:
        ASSERT_NOT_REACHED();
    }
}

static InputModifiers glfwModsToInputModifiers(int glfwMods)
{
    InputModifiers mods { 0 };

    if (glfwMods & GLFW_MOD_SHIFT) { 
        mods = mods | InputModifier::Shift;
    }

    if (glfwMods & GLFW_MOD_CONTROL) {
        mods = mods | InputModifier::Control;
    }

    if (glfwMods & GLFW_MOD_ALT) {
        mods = mods | InputModifier::Alt;
    }

    if (glfwMods & GLFW_MOD_SUPER) {
        mods = mods | InputModifier::Super;
    }

    if (glfwMods & GLFW_MOD_CAPS_LOCK) {
        mods = mods | InputModifier::CapsLock;
    }

    if (glfwMods & GLFW_MOD_NUM_LOCK) {
        mods = mods | InputModifier::NumLock;
    }

    return mods;
}

void SystemGlfw::keyEventCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    Input::mutableInstance().keyEventCallback(key, scancode, glfwActionToInputAction(action), glfwModsToInputModifiers(mods));
}

void SystemGlfw::mouseButtonEventCallback(GLFWwindow* window, int button, int action, int mods)
{
    Input::mutableInstance().mouseButtonEventCallback(button, glfwActionToInputAction(action), glfwModsToInputModifiers(mods));

    // HACK: This is a very application-specific hack.. remove from here!
    auto& system = *static_cast<SystemGlfw*>(glfwGetWindowUserPointer(window));
    glfwSetInputMode(system.m_glfwWindow, GLFW_CURSOR, Input::instance().isButtonDown(Button::Right) ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void SystemGlfw::mouseMovementEventCallback(GLFWwindow* window, double xPosition, double yPosition)
{
    Input::mutableInstance().mouseMovementEventCallback(xPosition, yPosition);
}

void SystemGlfw::mouseScrollEventCallback(GLFWwindow* window, double xOffset, double yOffset)
{
    Input::mutableInstance().mouseScrollEventCallback(xOffset, yOffset);
}
