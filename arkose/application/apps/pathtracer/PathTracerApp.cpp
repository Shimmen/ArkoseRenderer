#include "PathTracerApp.h"

// Nodes
#include "rendering/nodes/DebugDrawNode.h"
#include "rendering/nodes/FinalNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/TonemapNode.h"
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

void PathTracerApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    Scene::Description description { .withRayTracing = true };
    // NOTE: Scene not under "assets/sample/" will not be available in the Git-repo, either due to file size or license or both!
    //description.path = "assets/PicaPica/PicaPicaMiniDiorama.arklvl";
    description.path = "assets/sample/Sponza.arklvl";
    scene.setupFromDescription(description);

    if (scene.directionalLightCount() == 0) {
        DirectionalLight& sun = scene.addLight(std::make_unique<DirectionalLight>(vec3(1.0f), 90'000.0f, normalize(vec3(0.5f, -1.0f, 0.2f))));
        sun.transform().setTranslation({ 0.0f, 2.5f, 0.0f });
    }

    Camera& camera = scene.camera();
    m_fpsCameraController.takeControlOfCamera(camera);

    m_renderPipeline = &pipeline;
    constexpr bool debugNodes = true;

    if (debugNodes) {
        pipeline.addNode<PickingNode>();
    }

    pipeline.addNode<PathTracerNode>();
    std::string sceneTexture = "PathTracerAccumulation";

    pipeline.addNode<TonemapNode>(sceneTexture);
    std::string finalTextureToScreen = "SceneColorLDR";

    if (debugNodes) {
        pipeline.addNode<DebugDrawNode>();
    }

    auto& sharpeningNode = pipeline.addNode<CASNode>(finalTextureToScreen);
    sharpeningNode.setEnabled(false);

    auto& finalNode = pipeline.addNode<FinalNode>(finalTextureToScreen);
    finalNode.setRenderVignette(false);
    finalNode.setRenderFilmGrain(false);
}

bool PathTracerApp::update(Scene& scene, float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

    const Input& input = Input::instance();

    // Toggle GUI with the ` key
    if (input.wasKeyReleased(Key::GraveAccent)) {
        m_guiEnabled = !m_guiEnabled;
    }

    if (m_guiEnabled) {
        static bool showRenderPipelineGui = true;
        if (m_renderPipeline && showRenderPipelineGui) {
            if (ImGui::Begin("Render Pipeline", &showRenderPipelineGui)) {
                m_renderPipeline->drawGui();
            }
            ImGui::End();
        }
    }

    m_fpsCameraController.update(input, deltaTime);

    return true;
}
