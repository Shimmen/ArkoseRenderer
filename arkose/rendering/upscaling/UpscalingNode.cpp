#include "UpscalingNode.h"

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"

UpscalingNode::UpscalingNode(UpscalingTech tech, UpscalingQuality quality)
    : m_upscalingTech(tech)
    , m_upscalingQuality(quality)
{
}

std::string UpscalingNode::name() const
{
    switch (m_upscalingTech) {
#if WITH_DLSS
    case UpscalingTech::DLSS:
        return "DLSS";
#endif
    case UpscalingTech::None:
    default:
        ASSERT_NOT_REACHED();
        return "";
    }
}

void UpscalingNode::drawGui()
{
    if (m_upscalingState == nullptr) {
        return;
    }

    UpscalingQuality quality = m_upscalingState->quality();

    // TODO: Make nice GUI for quality selector! When quality changes, make sure we request a the pipeline to reconstruct!

    if (quality != m_upscalingState->quality()) {
        m_upscalingState->setQuality(quality);
    }
}

RenderPipelineNode::ExecuteCallback UpscalingNode::construct(GpuScene& scene, Registry& reg)
{
    Texture::Description upscaledSceneColorDesc = reg.getTexture("SceneColor")->description();
    upscaledSceneColorDesc.extent = pipeline().outputResolution();
    Texture& upscaledSceneColorTex = reg.createTexture(upscaledSceneColorDesc);
    reg.publish("SceneColorUpscaled", upscaledSceneColorTex);

    m_upscalingState = &reg.createUpscalingState(m_upscalingTech, m_upscalingQuality,
                                                 pipeline().renderResolution(),
                                                 pipeline().outputResolution());

    UpscalingParameters& params = reg.allocate<UpscalingParameters>();
    params.upscaledColor = &upscaledSceneColorTex;
    params.inputColor = reg.getTexture("SceneColor");
    params.depthTexture = reg.getTexture("SceneDepth");
    params.velocityTexture = reg.getTexture("SceneNormalVelocity"); // TODO: NEEDS REMAPPING TO A RG16F FORMAT WITH VELOCITY IN THE RG-components!
                                                                    // OR, we create a custom image view / sampler with a rg-ba swapping swizzle?
    params.exposureTexture = nullptr; // we're not using auto-exposure for now.

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        scene.camera().setFrustumJitteringEnabled(true);

        params.preExposure = scene.lightPreExposure();
        params.frustumJitterOffset = scene.camera().frustumJitterPixelOffset();

        params.sharpness = m_upscalingState->optimalSharpness().value_or(1.0f);

        // TODO: Or camera cut, once we add this kind of functionality..
        params.resetAccumulation = appState.isRelativeFirstFrame();

        cmdList.evaluateUpscaling(*m_upscalingState, params);
        cmdList.textureWriteBarrier(*params.upscaledColor);
    };
}
