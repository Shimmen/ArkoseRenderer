#include "Arkose.h"

#include "core/Logging.h"
#include "core/parallel/TaskGraph.h"
#include "physics/PhysicsScene.h"
#include "physics/backend/base/PhysicsBackend.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "system/System.h"
#include "utility/CommandLine.h"
#include "utility/Profiling.h"

// Apps - kind of like demos / applets(?) that can run within Arkose.
// The idea is that all of them are compiled in by default, and you
// can run the engine in different modes by launching these apps.
// Eventually I want to be able to launch & switch between these
// in runtime, so we have a nice and usable environment, both for
// editor purposes but potentially also for different game "views".
#include "application/apps/geodata/GeodataApp.h"
#include "application/apps/BootstrappingApp.h"
#include "application/apps/MeshViewerApp.h"
#include "application/apps/ShowcaseApp.h"
#include "application/apps/SSSDemo.h"

static std::unique_ptr<App> createApp()
{
    if (CommandLine::hasArgument("-meshviewer")) {
        return std::make_unique<MeshViewerApp>();
    }
    if (CommandLine::hasArgument("-sssdemo")) {
        return std::make_unique<SSSDemo>();
    }
    if (CommandLine::hasArgument("-geodata")) {
        return std::make_unique<GeodataApp>();
    }

    return std::make_unique<ShowcaseApp>();
}

int Arkose::runArkoseApplication(int argc, char** argv)
{
    // Initialize core systems
    CommandLine::initialize(argc, argv);
    TaskGraph::initialize();
    System::initialize();

    System& system = System::get();

    // Create window & input handling for that window
    system.createWindow(System::WindowType::Windowed, { 1920, 1080 });

    // Create the app that will drive this "engine"
    auto app = createApp();
    Backend::AppSpecification appSpec;
    appSpec.requiredCapabilities = app->requiredCapabilities();
    appSpec.optionalCapabilities = app->optionalCapabilities();

    // Create backends
    Backend& graphicsBackend = Backend::create(appSpec);
    PhysicsBackend* physicsBackend = PhysicsBackend::create();

    // Create the scene
    auto scene = std::make_unique<Scene>(graphicsBackend, physicsBackend);

    // Let the app define the render pipeline and push it to the graphics backend
    auto renderPipeline = std::make_unique<RenderPipeline>(&scene->gpuScene());
    renderPipeline->setOutputResolution(system.windowFramebufferSize());
    renderPipeline->setRenderResolution(system.windowFramebufferSize());
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

        system.newFrame();
        graphicsBackend.newFrame();

        Extent2D viewportSize = system.windowFramebufferSize();
        if (viewportSize != currentViewportSize) {
            currentViewportSize = viewportSize;
            renderPipeline->setOutputResolution(viewportSize);

            Extent2D windowSize = system.windowSize();
            scene->camera().setTargetWindowSize(windowSize);
        }

        float elapsedTime = static_cast<float>(system.timeSinceStartup());
        float deltaTime = elapsedTime - lastTime;
        lastTime = elapsedTime;

        bool keepRunning = app->update(*scene, elapsedTime, deltaTime);
        exitRequested = !keepRunning || system.exitRequested();

        scene->update(elapsedTime, deltaTime);

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

    // Shutdown core systems
    TaskGraph::shutdown();
    System::shutdown();
    CommandLine::shutdown();

    return 0;
}
