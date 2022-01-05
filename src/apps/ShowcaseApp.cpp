#include "ShowcaseApp.h"

#include "rendering/nodes/AutoExposureNode.h"
#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/CullingNode.h"
#include "rendering/nodes/DDGINode.h"
#include "rendering/nodes/DDGIProbeDebug.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/FXAANode.h"
#include "rendering/nodes/GIComposeNode.h"
#include "rendering/nodes/PrepassNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/RTDirectLightNode.h"
#include "rendering/nodes/RTFirstHitNode.h"
#include "rendering/nodes/SceneNode.h"
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

std::vector<Backend::Capability> ShowcaseApp::requiredCapabilities()
{
    return { Backend::Capability::RtxRayTracing };
}

void ShowcaseApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    scene.setShouldMaintainRayTracingScene(true);
    scene.loadFromFile("assets/sample/sponza.json");

    if (!scene.hasProbeGrid()) {
        scene.generateProbeGridFromBoundingBox();
    }

    pipeline.addNode<SceneNode>(scene);
    pipeline.addNode<PickingNode>(scene);

    pipeline.addNode<DDGINode>(scene);

    pipeline.addNode<ShadowMapNode>(scene);
    pipeline.addNode<CullingNode>(scene);
    pipeline.addNode<PrepassNode>(scene);
    pipeline.addNode<ForwardRenderNode>(scene);

    pipeline.addNode<SSAONode>(scene);
    pipeline.addNode<GIComposeNode>(scene);
    
    pipeline.addNode<SkyViewNode>(scene);
    pipeline.addNode<BloomNode>(scene);

    pipeline.addNode<DDGIProbeDebug>(scene);

    std::string finalTexture = "SceneColor";
    AntiAliasing antiAliasingMode = AntiAliasing::TAA;

    // Uncomment for ray tracing visualisations
    //pipeline.addNode<RTFirstHitNode>(scene); finalTexture = "RTFirstHit";
    //pipeline.addNode<RTDirectLightNode>(scene); finalTexture = "RTDirectLight";

    auto tonemapMode = (antiAliasingMode == AntiAliasing::None)
        ? TonemapNode::Mode::RenderToWindow
        : TonemapNode::Mode::RenderToSceneColorLDR;
    pipeline.addNode<TonemapNode>(scene, finalTexture, tonemapMode);

    switch (antiAliasingMode) {
    case AntiAliasing::FXAA:
        pipeline.addNode<FXAANode>(scene);
        break;
    case AntiAliasing::TAA:
        pipeline.addNode<TAANode>(scene);
        break;
    }
}

void ShowcaseApp::update(Scene& scene, float elapsedTime, float deltaTime)
{
    float sunRotation = 0.0f;
    sunRotation -= Input::instance().isKeyDown(Key::Left) ? 1.0f : 0.0f;
    sunRotation += Input::instance().isKeyDown(Key::Right) ? 1.0f : 0.0f;
    quat rotation = axisAngle(moos::globalRight, sunRotation * deltaTime * 0.2f);
    scene.sun().direction = moos::rotateVector(rotation, scene.sun().direction);
}
