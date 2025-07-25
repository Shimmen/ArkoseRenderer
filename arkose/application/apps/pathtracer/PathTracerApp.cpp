#include "PathTracerApp.h"

// Nodes
#include "rendering/nodes/DebugDrawNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/output/OutputNode.h"
#include "rendering/pathtracer/PathTracerNode.h"
#include "rendering/postprocess/CASNode.h"

#include "scene/Scene.h"
#include "scene/lights/DirectionalLight.h"
#include "system/Input.h"
#include "utility/Profiling.h"
#include <imgui.h>

std::vector<Backend::Capability> PathTracerApp::requiredCapabilities()
{
    return { Backend::Capability::RayTracing };
}

void PathTracerApp::setup(Backend& graphicsBackend, PhysicsBackend* physicsBackend)
{
    SCOPED_PROFILE_ZONE();

    AppBase::setup(graphicsBackend, physicsBackend);
    Scene& scene = mainScene();

    Scene::Description description { .withRayTracing = true };
    // NOTE: Scene not under "assets/sample/" will not be available in the Git-repo, either due to file size or license or both!
    //description.path = "assets/PicaPica/PicaPicaMiniDiorama.arklvl";
    //description.path = "assets/sample/levels/Sponza.arklvl";
    description.path = "assets/sample/levels/CornellBox.arklvl";
    scene.setupFromDescription(description);

    if (scene.directionalLightCount() == 0) {
        DirectionalLight& sun = scene.addLight(std::make_unique<DirectionalLight>(Colors::white, 90'000.0f, normalize(vec3(0.5f, -1.0f, 0.2f))));
        sun.transform().setTranslation({ 0.0f, 2.5f, 0.0f });
    }

    Camera& camera = scene.camera();
    m_fpsCameraController.takeControlOfCamera(camera);

    RenderPipeline& pipeline = mainRenderPipeline();
    constexpr bool debugNodes = true;

    if (debugNodes) {
        pipeline.addNode<PickingNode>();
    }

    pipeline.addNode<PathTracerNode>();
    std::string sceneTexture = "PathTracerAccumulation";

    auto& outputNode = pipeline.addNode<OutputNode>(sceneTexture);
    outputNode.setRenderVignette(false);
    outputNode.setRenderFilmGrain(false);

    if (debugNodes) {
        pipeline.addNode<DebugDrawNode>();
    }
}

bool PathTracerApp::update(float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

    AppBase::update(elapsedTime, deltaTime);

    const Input& input = Input::instance();

    // Toggle GUI with the ` key
    if (input.wasKeyReleased(Key::GraveAccent)) {
        m_guiEnabled = !m_guiEnabled;
    }

    if (m_guiEnabled) {
        static bool showRenderPipelineGui = true;
        if (showRenderPipelineGui) {
            if (ImGui::Begin("Render Pipeline", &showRenderPipelineGui)) {
                mainRenderPipeline().drawGui();
            }
            ImGui::End();
        }
    }

    m_fpsCameraController.update(input, deltaTime);

    return true;
}

void PathTracerApp::render(Backend& graphicsBackend, float elapsedTime, float deltaTime)
{
    AppBase::render(graphicsBackend, elapsedTime, deltaTime);
}
