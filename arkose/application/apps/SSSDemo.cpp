#include "SSSDemo.h"

#include "system/Input.h"
#include "rendering/forward/ForwardRenderNode.h"
#include "rendering/forward/PrepassNode.h"
#include "rendering/lighting/LightingComposeNode.h"
#include "rendering/meshlet/MeshletVisibilityBufferRenderNode.h"
#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/DDGINode.h"
#include "rendering/nodes/DDGIProbeDebug.h"
#include "rendering/nodes/DebugDrawNode.h"
#include "rendering/nodes/DepthOfFieldNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/RTReflectionsNode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/nodes/TAANode.h"
#include "rendering/nodes/VisibilityBufferShadingNode.h"
#include "rendering/output/OutputNode.h"
#include "rendering/postprocess/SSSSNode.h"
#include "rendering/shadow/DirectionalShadowDrawNode.h"
#include "rendering/shadow/DirectionalShadowProjectNode.h"
#include "rendering/shadow/LocalShadowDrawNode.h"
#include "rendering/upscaling/UpscalingNode.h"
#include "scene/Scene.h"
#include "scene/camera/Camera.h"
#include "scene/lights/DirectionalLight.h"
#include "shaders/shared/TonemapData.h"
#include "utility/Profiling.h"
#include <ark/random.h>
#include <imgui.h>

std::vector<Backend::Capability> SSSDemo::requiredCapabilities()
{
    return { Backend::Capability::RayTracing, Backend::Capability::MeshShading };
}

void SSSDemo::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    scene.setupFromDescription({ .path = "assets/sample/levels/SSSDemo/SSSDemo.arklvl",
                                 .withRayTracing = true,
                                 .withMeshShading = true });

    // Generate panels
    {
        auto generateQuadMesh = [&](std::string name, std::string materialName) -> StaticMeshInstance& {
            MeshAsset* meshAsset = new MeshAsset();
            meshAsset->name = std::move(name);

            MeshLODAsset& lod0 = meshAsset->LODs.emplace_back();
            MeshSegmentAsset& segment = lod0.meshSegments.emplace_back();

            segment.material = std::move(materialName);

            segment.positions = { { -0.5f, -0.5f, 0.0f },
                                  { +0.5f, -0.5f, 0.0f },
                                  { +0.5f, +0.5f, 0.0f },
                                  { -0.5f, +0.5f, 0.0f } };
            segment.normals = { { 0.0f, 0.0f, 1.0f },
                                { 0.0f, 0.0f, 1.0f },
                                { 0.0f, 0.0f, 1.0f },
                                { 0.0f, 0.0f, 1.0f } };
            segment.texcoord0s = { { 0.0f, 0.0f },
                                   { 1.0f, 0.0f },
                                   { 1.0f, 1.0f },
                                   { 0.0f, 1.0f } };
            segment.indices = { 0, 1, 3, 1, 2, 3 };

            segment.generateMeshlets();

            meshAsset->boundingBox.min = { -0.5f, -0.5f, 0.0 };
            meshAsset->boundingBox.max = { +0.5f, +0.5f, 0.0 };
            meshAsset->boundingSphere = geometry::Sphere({ 0.0f, 0.0f, 0.0f }, 1.0f);

            return scene.addMesh(meshAsset);
        };

        StaticMeshInstance& panelM = generateQuadMesh("light-panel", "assets/sample/levels/SSSDemo/light-panel.arkmat");
        panelM.transform().setScale({ 0.65f, 2.5f, 1.0f });
        //panelM.transform().setPositionInWorld({ -0.5f, 0.0f, -1.5f });
        //panelM.transform().setOrientation(ark::axisAngle(ark::globalUp, ark::toRadians(40.0f)));
        panelM.transform().setPositionInWorld({ 0.0f, 0.0f, -1.3f });

        StaticMeshInstance& panelL = generateQuadMesh("green-panel", "assets/sample/levels/SSSDemo/color-panel-g.arkmat");
        panelL.transform().setScale({ 1.0f, 2.5f, 1.0f });
        panelL.transform().setPositionInWorld({ -0.6f, 0.0f, -0.5f });
        panelL.transform().setOrientation(ark::axisAngle(ark::globalUp, ark::toRadians(+75.0f)));

        StaticMeshInstance& panelR = generateQuadMesh("red-panel", "assets/sample/levels/SSSDemo/color-panel-r.arkmat");
        panelR.transform().setScale({ 1.0f, 2.5f, 1.0f });
        panelR.transform().setPositionInWorld({ +0.6f, 0.0f, -0.5f });
        panelR.transform().setOrientation(ark::axisAngle(ark::globalUp, ark::toRadians(-75.0f)));
    }

    scene.generateProbeGridFromBoundingBox();

    //

    m_cameraController.takeControlOfCamera(scene.camera());
    m_cameraController.setMaxSpeed(0.5f);

    pipeline.addNode<PickingNode>();

    pipeline.addNode<DDGINode>();

    pipeline.addNode<MeshletVisibilityBufferRenderNode>();

    pipeline.addNode<DirectionalShadowDrawNode>();
    pipeline.addNode<DirectionalShadowProjectNode>();
    pipeline.addNode<LocalShadowDrawNode>();

    pipeline.addNode<VisibilityBufferShadingNode>();

    auto& rtReflectionsNode = pipeline.addNode<RTReflectionsNode>();
    rtReflectionsNode.setNoTracingRoughnessThreshold(1.0f);

    pipeline.addNode<SSSSNode>();
    pipeline.addNode<LightingComposeNode>();

    pipeline.addNode<SkyViewNode>();
    scene.setEnvironmentMap({ .assetPath = "", .brightnessFactor = 500.0f });

    auto& dofNode = pipeline.addNode<DepthOfFieldNode>();
    dofNode.setEnabled(true);

    pipeline.addNode<BloomNode>();

    pipeline.addNode<DDGIProbeDebug>();

    const std::string sceneTexture = "SceneColor";
    const std::string finalTextureToScreen = "SceneColorLDR";

    pipeline.addNode<TAANode>(scene.camera());

    OutputNode& outputNode = pipeline.addNode<OutputNode>(sceneTexture);
    outputNode.setTonemapMethod(TONEMAP_METHOD_KHRONOS_PBR_NEUTRAL);
    outputNode.setRenderFilmGrain(false);

    pipeline.addNode<DebugDrawNode>();

    m_renderPipeline = &pipeline;
}

bool SSSDemo::update(Scene& scene, float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

    const Input& input = Input::instance();

    // Toggle GUI with the ` key
    if (input.wasKeyReleased(Key::GraveAccent)) {
        m_guiEnabled = !m_guiEnabled;
    }

    if (m_guiEnabled) {
        if (ImGui::Begin("Render Pipeline")) {
            m_renderPipeline->drawGui();
        }
        ImGui::End();
    }

    m_cameraController.update(input, deltaTime);

    float sunRotation = 0.0f;
    sunRotation -= input.isKeyDown(Key::Left) ? 1.0f : 0.0f;
    sunRotation += input.isKeyDown(Key::Right) ? 1.0f : 0.0f;
    quat rotation = axisAngle(ark::globalRight, sunRotation * deltaTime * 0.35f);
    if (DirectionalLight* sun = scene.firstDirectionalLight()) {
        sun->transform().setOrientation(rotation * sun->transform().localOrientation());
    }

    return true;
}
