#include "backend/Backend.h"
#include "backend/vulkan/VulkanBackend.h"
#include "rendering/App.h"
#include "rendering/ShaderManager.h"
#include "utility/Input.h"
#include "utility/Logging.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app-selector.h"

enum class WindowType {
    Windowed,
    Fullscreen
};

GLFWwindow* createWindow(Backend::Type backendType, WindowType windowType, const Extent2D& windowSize)
{
    std::string windowTitle = "Arkose Renderer";

    switch (backendType) {
    case Backend::Type::Vulkan:
        if (!glfwVulkanSupported()) {
            LogErrorAndExit("ArkoseRenderer::createWindow(): Vulkan is not supported but the Vulkan backend is requested, exiting.\n");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        windowTitle += " [Vulkan]";
        break;
    }

    GLFWwindow* window = nullptr;

    switch (windowType) {
    case WindowType::Fullscreen: {
        GLFWmonitor* defaultMonitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* defaultVideoMode = glfwGetVideoMode(defaultMonitor);
        window = glfwCreateWindow(defaultVideoMode->width, defaultVideoMode->height, windowTitle.c_str(), defaultMonitor, nullptr);
        break;
    }
    case WindowType::Windowed: {
        window = glfwCreateWindow(windowSize.width(), windowSize.height(), windowTitle.c_str(), nullptr, nullptr);
        break;
    }
    }

    if (!window) {
        LogErrorAndExit("ArkoseRenderer::createWindow(): could not create GLFW window with specified settings, exiting.\n");
    }

    return window;
}

std::unique_ptr<Backend> createBackend(Backend::Type backendType, GLFWwindow* window, App& app)
{
    std::unique_ptr<Backend> backend;

    switch (backendType) {
    case Backend::Type::Vulkan:
        backend = std::make_unique<VulkanBackend>(window, app);
        break;
    }

    return backend;
}

int main(int argc, char** argv)
{
    if (!glfwInit()) {
        LogErrorAndExit("ArkoseRenderer::main(): could not initialize GLFW, exiting.\n");
    }

    auto backendType = Backend::Type::Vulkan;
    GLFWwindow* window = createWindow(backendType, WindowType::Windowed, { 1920, 1080 });
    Input::registerWindow(window);

    {
        auto app = std::make_unique<SelectedApp>();
        auto backend = createBackend(backendType, window, *app);

        LogInfo("ArkoseRenderer: main loop begin.\n");

        ShaderManager::instance().startFileWatching(250, []() {
            LogInfo("One or more shader files updated!\n");
            LogError("FIXME: Respond to shader file updates!\n");
        });

        glfwSetTime(0.0);
        double lastTime = 0.0;
        while (!glfwWindowShouldClose(window)) {

            Input::preEventPoll();
            glfwPollEvents();

            double elapsedTime = glfwGetTime();
            double deltaTime = elapsedTime - lastTime;
            lastTime = elapsedTime;

            bool frameExecuted = false;
            while (!frameExecuted) {
                frameExecuted = backend->executeFrame(elapsedTime, deltaTime);
            }
        }

        ShaderManager::instance().stopFileWatching();
        LogInfo("ArkoseRenderer: main loop end.\n");
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
