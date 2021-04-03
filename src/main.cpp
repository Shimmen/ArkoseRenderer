#include "backend/Backend.h"
#include "backend/vulkan/VulkanBackend.h"
#include "rendering/App.h"
#include "rendering/ShaderManager.h"
#include "utility/Input.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app-selector.h"

enum class WindowType {
    Windowed,
    Fullscreen
};

GLFWwindow* createWindow(Backend::Type backendType, WindowType windowType, const Extent2D& windowSize)
{
    SCOPED_PROFILE_ZONE();

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

std::unique_ptr<Backend> createBackend(Backend::Type backendType, GLFWwindow* window, const Backend::AppSpecification& appSpecification)
{
    SCOPED_PROFILE_ZONE();

    std::unique_ptr<Backend> backend;

    switch (backendType) {
    case Backend::Type::Vulkan:
        backend = std::make_unique<VulkanBackend>(window, appSpecification);
        break;
    }

    return backend;
}

int main(int argc, char** argv)
{
    SCOPED_PROFILE_ZONE();

    if (!glfwInit()) {
        LogErrorAndExit("ArkoseRenderer::main(): could not initialize GLFW, exiting.\n");
    }

    auto backendType = Backend::Type::Vulkan;
    GLFWwindow* window = createWindow(backendType, WindowType::Windowed, { 1920, 1080 });
    Input::registerWindow(window);

    auto app = std::make_unique<SelectedApp>();

    Backend::AppSpecification appSpec;
    appSpec.requiredCapabilities = app->requiredCapabilities();
    appSpec.optionalCapabilities = app->optionalCapabilities();
    auto backend = createBackend(backendType, window, appSpec);

    auto scene = std::make_unique<Scene>(backend->getPersistentRegistry());
    auto renderGraph = std::make_unique<RenderGraph>();
    app->setup(*scene, *renderGraph);
    backend->renderGraphDidChange(*renderGraph);

    LogInfo("ArkoseRenderer: main loop begin.\n");

    std::mutex shaderFileWatchMutex {};
    std::vector<std::string> changedShaderFiles {};
    ShaderManager::instance().startFileWatching(1'000, [&](const std::vector<std::string>& changedFiles) {
        shaderFileWatchMutex.lock();
        changedShaderFiles = changedFiles;
        shaderFileWatchMutex.unlock();
    });

    glfwSetTime(0.0);
    double lastTime = 0.0;
    while (!glfwWindowShouldClose(window)) {

        if (shaderFileWatchMutex.try_lock()) {
            if (changedShaderFiles.size() > 0) {
                backend->shadersDidRecompile(changedShaderFiles, *renderGraph);
                changedShaderFiles.clear();
            }
            shaderFileWatchMutex.unlock();
        }

        Input::preEventPoll();
        glfwPollEvents();

        backend->newFrame(*scene);

        double elapsedTime = glfwGetTime();
        double deltaTime = elapsedTime - lastTime;
        lastTime = elapsedTime;

        scene->update((float)elapsedTime, (float)deltaTime);
        app->update(*scene, (float)elapsedTime, (float)deltaTime);

        bool frameExecuted = false;
        while (!frameExecuted) {
            frameExecuted = backend->executeFrame(*scene, *renderGraph, elapsedTime, deltaTime);
        }

        END_OF_FRAME_PROFILE_MARKER();
    }

    ShaderManager::instance().stopFileWatching();
    LogInfo("ArkoseRenderer: main loop end.\n");

    // Best to make sure the backend is destroyed before the window as it relies on it
    backend.reset();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
