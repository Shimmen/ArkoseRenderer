#include "apps/App.h"
#include "apps/MeshViewerApp.h"
#include "apps/ShowcaseApp.h"
#include "core/Logging.h"
#include "core/parallel/TaskGraph.h"
#include "physics/PhysicsScene.h"
#include "physics/backend/base/PhysicsBackend.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/backend/shader/ShaderManager.h"
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

    if (!glfwInit()) {
        ARKOSE_LOG(Fatal, "could not initialize windowing system, exiting.");
    }

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
        ARKOSE_LOG(Fatal, "could not create window with specified settings, exiting.");
    }

    return window;
}

Extent2D windowFramebufferSize(GLFWwindow* window)
{
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    return Extent2D(width, height);
}

std::unique_ptr<App> createApp(const std::vector<std::string> arguments)
{
    if (std::find(arguments.begin(), arguments.end(), "-meshviewer") != arguments.end()) {
        return std::make_unique<MeshViewerApp>();
    }

    return std::make_unique<ShowcaseApp>();
}

int main(int argc, char** argv)
{
    std::vector<std::string> arguments;
    for (int idx = 1; idx < argc; ++idx) {
        arguments.emplace_back(argv[idx]);
    }

    // Grab relevant info from settings.h
    auto backendType = SelectedBackendType;
    auto physicsBackendType = SelectedPhysicsBackendType;

    // Initialize core systems
    TaskGraph::initialize();

    // Create window & input handling for that window
    GLFWwindow* window = createWindow(backendType, WindowType::Windowed, { 1920, 1080 });
    Input::registerWindow(window);

    // Create the app that will drive this "engine"
    auto app = createApp(arguments);
    Backend::AppSpecification appSpec;
    appSpec.requiredCapabilities = app->requiredCapabilities();
    appSpec.optionalCapabilities = app->optionalCapabilities();

    // Create backends
    Backend& graphicsBackend = Backend::create(backendType, window, appSpec);
    PhysicsBackend* physicsBackend = PhysicsBackend::create(physicsBackendType);

    // Create the scene
    auto scene = std::make_unique<Scene>(graphicsBackend, physicsBackend, windowFramebufferSize(window));

    // Let the app define the render pipeline and push it to the graphics backend
    auto renderPipeline = std::make_unique<RenderPipeline>(&scene->gpuScene());
    app->setup(*scene, *renderPipeline);
    graphicsBackend.renderPipelineDidChange(*renderPipeline);

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
    Extent2D currentViewportSize { 0, 0 };

    bool exitRequested = false;
    while (!exitRequested) {

        if (shaderFileWatchMutex.try_lock()) {
            if (changedShaderFiles.size() > 0) {
                graphicsBackend.shadersDidRecompile(changedShaderFiles, *renderPipeline);
                changedShaderFiles.clear();
            }
            shaderFileWatchMutex.unlock();
        }

        Input::preEventPoll();
        glfwPollEvents();

        graphicsBackend.newFrame();

        Extent2D viewportSize = windowFramebufferSize(window);
        if (viewportSize != currentViewportSize) {
            currentViewportSize = viewportSize;
            scene->camera().setViewport(viewportSize);
        }

        float elapsedTime = static_cast<float>(glfwGetTime());
        float deltaTime = elapsedTime - lastTime;
        lastTime = elapsedTime;

        scene->update(elapsedTime, deltaTime);
        {
            SCOPED_PROFILE_ZONE_NAME_AND_COLOR("App update", 0xff00ff);
            bool keepRunning = app->update(*scene, elapsedTime, deltaTime);

            exitRequested |= !keepRunning;
            exitRequested |= static_cast<bool>(glfwWindowShouldClose(window));
        }

        if (physicsBackend) {
            physicsBackend->update(elapsedTime, deltaTime);
        }

        scene->preRender();

        bool frameExecuted = false;
        while (!frameExecuted) {
            frameExecuted = graphicsBackend.executeFrame(*renderPipeline, elapsedTime, deltaTime);
        }

        scene->postRender();

        END_OF_FRAME_PROFILE_MARKER();
    }

    ShaderManager::instance().stopFileWatching();
    ARKOSE_LOG(Info, "main loop end.");

    // Destroy the scene (ensure that all GPU stuff are completed first)
    graphicsBackend.completePendingOperations();
    renderPipeline.reset();
    scene.reset();

    // Destroy backends
    Backend::destroy();
    PhysicsBackend::destroy();

    // Destroy window & windowing system
    glfwDestroyWindow(window);
    glfwTerminate();

    // Shutdown core systems
    TaskGraph::shutdown();

    return 0;
}
