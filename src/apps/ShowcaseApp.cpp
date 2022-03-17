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
#include "rendering/nodes/RTReflectionsNode.h"
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
        return { Backend::Capability::RayTracing };
    } else {
        return {};
    }
}

void ShowcaseApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    scene.setupFromDescription({ .path = "assets/sample/sponza.json",
                                 .maintainRayTracingScene = rtxOn });

    if (!scene.hasProbeGrid()) {
        scene.generateProbeGridFromBoundingBox();
    }

    pipeline.addNode<PickingNode>();

    if (rtxOn) {
        pipeline.addNode<DDGINode>();
    } else {
        scene.setAmbientIlluminance(250.0f);
    }

    pipeline.addNode<ShadowMapNode>();
    pipeline.addNode<CullingNode>();
    pipeline.addNode<PrepassNode>();
    pipeline.addNode<ForwardRenderNode>();

    if (rtxOn) {
        pipeline.addNode<RTReflectionsNode>();
    }

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
    // TODO: The scene should contain a Camera which doesn't have controls while the app has a CameraController which does.
    //       Here we would then just update the controller which somehow changes the Camera it's assigned to control.
    scene.camera().update(Input::instance(), deltaTime);

    float sunRotation = 0.0f;
    sunRotation -= Input::instance().isKeyDown(Key::Left) ? 1.0f : 0.0f;
    sunRotation += Input::instance().isKeyDown(Key::Right) ? 1.0f : 0.0f;
    quat rotation = axisAngle(moos::globalRight, sunRotation * deltaTime * 0.2f);

    DirectionalLight& sun = *scene.firstDirectionalLight();
    sun.direction = moos::rotateVector(rotation, sun.direction);
}
