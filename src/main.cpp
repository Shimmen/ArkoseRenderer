#include "backend/base/Backend.h"
#include "backend/shader/ShaderManager.h"
#include "rendering/App.h"
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

int main(int argc, char** argv)
{
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
    Backend& backend = Backend::create(backendType, window, appSpec);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    auto scene = std::make_unique<Scene>(backend, backend.getPersistentRegistry(), Extent2D(width, height));
    auto renderPipeline = std::make_unique<RenderPipeline>(scene.get());

    app->setup(*scene, *renderPipeline);
    backend.renderPipelineDidChange(*renderPipeline);

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
    bool firstFrame = true;
    while (!glfwWindowShouldClose(window)) {

        if (shaderFileWatchMutex.try_lock()) {
            if (changedShaderFiles.size() > 0) {
                backend.shadersDidRecompile(changedShaderFiles, *renderPipeline);
                changedShaderFiles.clear();
            }
            shaderFileWatchMutex.unlock();
        }

        Input::preEventPoll();
        glfwPollEvents();

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        backend.newFrame();
        scene->newFrame({ width, height }, firstFrame);

        double elapsedTime = glfwGetTime();
        double deltaTime = elapsedTime - lastTime;
        lastTime = elapsedTime;

        app->update(*scene, static_cast<float>(elapsedTime), static_cast<float>(deltaTime));

        bool frameExecuted = false;
        while (!frameExecuted) {
            frameExecuted = backend.executeFrame(*scene, *renderPipeline, elapsedTime, deltaTime);
        }

        firstFrame = false;

        END_OF_FRAME_PROFILE_MARKER();
    }

    ShaderManager::instance().stopFileWatching();
    LogInfo("ArkoseRenderer: main loop end.\n");

    Backend::destroy();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
