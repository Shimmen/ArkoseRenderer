#include "backend/base/Backend.h"
#include "backend/shader/ShaderManager.h"
#include "core/Logging.h"
#include "core/parallel/TaskGraph.h"
#include "rendering/App.h"
#include "utility/Input.h"
#include "utility/Profiling.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "settings.h"

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
            ARKOSE_LOG(Fatal, "Vulkan is not supported but the Vulkan backend is requested, exiting.");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        windowTitle += " [Vulkan]";
        break;
    case Backend::Type::D3D12:
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        windowTitle += " [D3D12]";
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
        ARKOSE_LOG(Fatal, "could not create GLFW window with specified settings, exiting.");
    }

    return window;
}

Extent2D windowFramebufferSize(GLFWwindow* window)
{
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    return Extent2D(width, height);
}

int main(int argc, char** argv)
{
    TaskGraph::initialize();

    if (!glfwInit()) {
        ARKOSE_LOG(Fatal, "could not initialize GLFW, exiting.");
    }

    auto backendType = SelectedBackendType;
    GLFWwindow* window = createWindow(backendType, WindowType::Windowed, { 1920, 1080 });
    Input::registerWindow(window);

    auto app = std::make_unique<SelectedApp>();

    Backend::AppSpecification appSpec;
    appSpec.requiredCapabilities = app->requiredCapabilities();
    appSpec.optionalCapabilities = app->optionalCapabilities();
    Backend& backend = Backend::create(backendType, window, appSpec);

    auto scene = std::make_unique<Scene>(backend, windowFramebufferSize(window));
    auto renderPipeline = std::make_unique<RenderPipeline>(&scene->gpuScene());

    app->setup(*scene, *renderPipeline);
    backend.renderPipelineDidChange(*renderPipeline);

    ARKOSE_LOG(Info, "main loop begin.");

    std::mutex shaderFileWatchMutex {};
    std::vector<std::string> changedShaderFiles {};
    ShaderManager::instance().startFileWatching(1'000, [&](const std::vector<std::string>& changedFiles) {
        shaderFileWatchMutex.lock();
        changedShaderFiles = changedFiles;
        shaderFileWatchMutex.unlock();
    });

    glfwSetTime(0.0);
    float lastTime = 0.0f;
    bool firstFrame = true;

    bool exitRequested = false;
    while (!exitRequested) {

        if (shaderFileWatchMutex.try_lock()) {
            if (changedShaderFiles.size() > 0) {
                backend.shadersDidRecompile(changedShaderFiles, *renderPipeline);
                changedShaderFiles.clear();
            }
            shaderFileWatchMutex.unlock();
        }

        Input::preEventPoll();
        glfwPollEvents();

        backend.newFrame();
        scene->newFrame(windowFramebufferSize(window), firstFrame);

        float elapsedTime = static_cast<float>(glfwGetTime());
        float deltaTime = elapsedTime - lastTime;
        lastTime = elapsedTime;

        scene->update(elapsedTime, deltaTime);
        bool keepRunning = app->update(*scene, elapsedTime, deltaTime);

        exitRequested |= !keepRunning;
        exitRequested |= static_cast<bool>(glfwWindowShouldClose(window));

        bool frameExecuted = false;
        while (!frameExecuted) {
            frameExecuted = backend.executeFrame(*scene, *renderPipeline, elapsedTime, deltaTime);
        }

        firstFrame = false;

        END_OF_FRAME_PROFILE_MARKER();
    }

    ShaderManager::instance().stopFileWatching();
    ARKOSE_LOG(Info, "main loop end.");

    backend.shutdown();
    scene.reset();

    Backend::destroy();

    glfwDestroyWindow(window);
    glfwTerminate();

    TaskGraph::shutdown();

    return 0;
}
