#include "SystemGlfw.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include "system/Input.h"
#include "system/Gamepad.h"

// Dear ImGui & related
#include <imgui.h>
#include <implot.h>
#include <backends/imgui_impl_glfw.h>

#if defined(WITH_VULKAN)
#include <vulkan/vulkan.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(PLATFORM_WINDOWS)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

constexpr u32 MaxJoystickCount = GLFW_JOYSTICK_LAST + 1;

namespace {
std::array<GLFWgamepadstate, MaxJoystickCount> lastGamepadStates;
}

SystemGlfw::SystemGlfw()
{
    SCOPED_PROFILE_ZONE_SYSTEM();

    if (!glfwInit()) {
        ARKOSE_LOG(Fatal, "SystemGlfw: could not initialize glfw, exiting.");
    }
}

SystemGlfw::~SystemGlfw()
{
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    if (m_glfwWindow != nullptr) {
        glfwDestroyWindow(m_glfwWindow);
    }

    glfwTerminate();
}

bool SystemGlfw::createWindow(WindowType windowType, Extent2D const& requestedWindowSize, std::optional<u32> preferredMonitor)
{
    SCOPED_PROFILE_ZONE_SYSTEM();

    // NOTE: This is valid as long as we don't want an OpenGL or OpenGLES context (we support neither)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    std::string windowTitle = "Arkose";

    switch (windowType) {
    case WindowType::Fullscreen: {

        GLFWmonitor* monitor = nullptr;

        if (preferredMonitor.has_value()) {
            int preferredMonitorIdx = preferredMonitor.value();

            int monitorCount;
            GLFWmonitor** allMonitors = glfwGetMonitors(&monitorCount);
            if (preferredMonitorIdx >= 0 && preferredMonitorIdx < monitorCount) {
                monitor = allMonitors[preferredMonitorIdx];
            }
        }

        if (monitor == nullptr) {
            monitor = glfwGetPrimaryMonitor();
        }

        // Use the default / currently set video mode for the monitor.
        // This is likely what the monitor is set to in the OS so it should be reasonable.
        GLFWvidmode const* videoMode = glfwGetVideoMode(monitor);

        m_glfwWindow = glfwCreateWindow(videoMode->width, videoMode->height, windowTitle.c_str(), monitor, nullptr);
        break;
    }
    case WindowType::Windowed: {
        m_glfwWindow = glfwCreateWindow(requestedWindowSize.width(), requestedWindowSize.height(), windowTitle.c_str(), nullptr, nullptr);
        break;
    }
    }

    if (!m_glfwWindow) {
        ARKOSE_LOG(Fatal, "SystemGlfw: could not create window with specified settings, exiting.");
    }

    m_lastWindowSize = windowSize();

    // Set up input for the window
    glfwSetWindowUserPointer(m_glfwWindow, this);
    glfwSetKeyCallback(m_glfwWindow, SystemGlfw::keyEventCallback);
    glfwSetMouseButtonCallback(m_glfwWindow, SystemGlfw::mouseButtonEventCallback);
    glfwSetCursorPosCallback(m_glfwWindow, SystemGlfw::mouseMovementEventCallback);
    glfwSetScrollCallback(m_glfwWindow, SystemGlfw::mouseScrollEventCallback);

    // Enable raw mouse motion, if supported
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(m_glfwWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    // Set up Dear ImGui
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        style.Colors[ImGuiCol_MenuBarBg] = ImColor(255, 255, 255, 1);

        // NOTE: I think it should be fine to call this one, even if we're running on Vulkan,
        // but it would certainly feel better do actually call the correct one. It does mean
        // we can't use the ImGuiConfigFlags_ViewportsEnable feature, but I don't want it.
        // Anyway, be warned, this might blow up in my face :^)
        ImGui_ImplGlfw_InitForOther(m_glfwWindow, true);
        //ImGui_ImplGlfw_InitForVulkan(m_glfwWindow, true);
    }

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

bool SystemGlfw::newFrame()
{
    SCOPED_PROFILE_ZONE_SYSTEM();

    {
        SCOPED_PROFILE_ZONE_SYSTEM_NAMED("Poll events");

        Input::mutableInstance().preEventPoll();
        glfwPollEvents(); // will trigger calls to the event callbacks immediately

        // glfw doesn't use callbacks for joysticks / gamepads, needs to be polled manually
        collectGamepadState();
    }

    Extent2D currentWindowSize = windowSize();
    bool windowSizeDidChange = currentWindowSize != m_lastWindowSize;
    m_lastWindowSize = currentWindowSize;

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    return windowSizeDidChange;
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

void SystemGlfw::collectGamepadState()
{
    Input& input = Input::mutableInstance();

    for (int joystickIdx = GLFW_JOYSTICK_1; joystickIdx <= GLFW_JOYSTICK_LAST; ++joystickIdx) {
        if (!glfwJoystickPresent(joystickIdx)) {
            input.setGamepadInactive(joystickIdx);
            continue;
        }

        // What about non-gamepad joysticks?
        if (!glfwJoystickIsGamepad(joystickIdx)) {
            input.setGamepadInactive(joystickIdx);
            continue;
        }

        GLFWgamepadstate glfwGamepadState;
        if (glfwGetGamepadState(joystickIdx, &glfwGamepadState) != GLFW_FALSE) {

            vec2 leftStick = vec2(glfwGamepadState.axes[GLFW_GAMEPAD_AXIS_LEFT_X], -glfwGamepadState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
            vec2 rightStick = vec2(glfwGamepadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], -glfwGamepadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);

            float leftTrigger = glfwGamepadState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER];
            float rightTrigger = glfwGamepadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER];

            GamepadState gamepadState { leftStick, rightStick, leftTrigger, rightTrigger };
            Input::mutableInstance().setGamepadState(joystickIdx, gamepadState);

            lastGamepadStates[joystickIdx] = glfwGamepadState;
        }
    }
}
