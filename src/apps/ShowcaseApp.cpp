#include "ShowcaseApp.h"

#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/CullingNode.h"
#include "rendering/nodes/DDGINode.h"
#include "rendering/nodes/DDGIProbeDebug.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/FinalNode.h"
#include "rendering/nodes/FXAANode.h"
#include "rendering/nodes/GIComposeNode.h"
#include "rendering/nodes/PrepassNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/RTDirectLightNode.h"
#include "rendering/nodes/RTFirstHitNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/nodes/SSAONode.h"
#include "rendering/nodes/TAANode.h"
#include "rendering/nodes/TonemapNode.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/Input.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

constexpr bool keepRenderDocCompatible = false;
constexpr bool rtxOn = true && !keepRenderDocCompatible;

std::vector<Backend::Capability> ShowcaseApp::requiredCapabilities()
{
    if (rtxOn) {
        return { Backend::Capability::RtxRayTracing };
    } else {
        return {};
    }
}

void ShowcaseApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    scene.setShouldMaintainRayTracingScene(rtxOn);
    scene.loadFromFile("assets/sample/sponza.json");

    if (!scene.hasProbeGrid()) {
        scene.generateProbeGridFromBoundingBox();
    }

    pipeline.addNode<PickingNode>();

    if (rtxOn) {
        pipeline.addNode<DDGINode>();
    } else {
        scene.setAmbient(250.0f);
    }

    pipeline.addNode<ShadowMapNode>();
    pipeline.addNode<CullingNode>();
    pipeline.addNode<PrepassNode>();
    pipeline.addNode<ForwardRenderNode>();

    pipeline.addNode<SSAONode>();
    pipeline.addNode<GIComposeNode>();
    
    pipeline.addNode<SkyViewNode>();
    pipeline.addNode<BloomNode>();

    if (rtxOn) {
        pipeline.addNode<DDGIProbeDebug>();
    }

    std::string sceneTexture = "SceneColor";
    const std::string finalTextureToScreen = "SceneColorLDR";
    const AntiAliasing antiAliasingMode = AntiAliasing::TAA;

    if (rtxOn) {
        // Uncomment for ray tracing visualisations
        //pipeline.addNode<RTFirstHitNode>(); sceneTexture = "RTFirstHit";
        //pipeline.addNode<RTDirectLightNode>(); sceneTexture = "RTDirectLight";
    }

    pipeline.addNode<TonemapNode>(sceneTexture);

    switch (antiAliasingMode) {
    case AntiAliasing::FXAA:
        pipeline.addNode<FXAANode>();
        break;
    case AntiAliasing::TAA:
        pipeline.addNode<TAANode>(scene.camera());
        break;
    }

    pipeline.addNode<FinalNode>(finalTextureToScreen);
}

void ShowcaseApp::update(Scene& scene, float elapsedTime, float deltaTime)
{
    float sunRotation = 0.0f;
    sunRotation -= Input::instance().isKeyDown(Key::Left) ? 1.0f : 0.0f;
    sunRotation += Input::instance().isKeyDown(Key::Right) ? 1.0f : 0.0f;
    quat rotation = axisAngle(moos::globalRight, sunRotation * deltaTime * 0.2f);
    scene.sun().direction = moos::rotateVector(rotation, scene.sun().direction);
}
