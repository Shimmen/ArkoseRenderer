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
    }
}

void UpscalingNode::drawGui()
{
    if (m_upscalingState == nullptr) {
        return;
    }

    ImGui::Checkbox("Upscaling enabled", &m_enabled);

    UpscalingQuality quality = m_upscalingState->quality();

    // TODO: Make nice GUI for quality selector! When quality changes, make sure we request the pipeline to reconstruct!

    if (quality != m_upscalingState->quality()) {
        m_upscalingState->setQuality(quality);
    }

    Extent2D renderRes = m_upscalingState->renderResolution();
    Extent2D outputRes = m_upscalingState->outputResolution();
    float upscaleFactor = static_cast<float>(renderRes.width()) / static_cast<float>(outputRes.width());
    ImGui::Text("%ux%u -> %ux%u (%.2f render scale)",
                renderRes.width(), renderRes.height(),
                outputRes.width(), outputRes.height(),
                upscaleFactor);

    if (ImGui::TreeNode("Advanced")) {
        ImGui::Checkbox("Let upscaling control global mip-bias", &m_controlGlobalMipBias);
        ImGui::TreePop();
    }
}

RenderPipelineNode::ExecuteCallback UpscalingNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& sceneColorTex = *reg.getTexture("SceneColor");
    Texture::Description upscaledSceneColorDesc = sceneColorTex.description();
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
    params.velocityTexture = reg.getTexture("SceneNormalVelocity");
    params.velocityTextureIsSceneNormalVelocity = true;
    params.exposureTexture = nullptr; // we're not using auto-exposure for now.

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (!m_enabled) {
            scene.camera().setFrustumJitteringEnabled(false);
            cmdList.copyTexture(sceneColorTex, upscaledSceneColorTex);
            cmdList.textureWriteBarrier(*params.upscaledColor);
            return;
        }

        scene.camera().setFrustumJitteringEnabled(true);
        if (m_controlGlobalMipBias) {
            float recommendedMipBias = m_upscalingState->optimalMipBias();
            scene.setGlobalMipBias(recommendedMipBias);
        }

        params.preExposure = scene.lightPreExposure();
        params.frustumJitterOffset = scene.camera().frustumJitterPixelOffset();

        params.sharpness = m_upscalingState->optimalSharpness().value_or(1.0f);

        // TODO: Or camera cut, once we add this kind of functionality..
        params.resetAccumulation = appState.isRelativeFirstFrame();

        cmdList.evaluateUpscaling(*m_upscalingState, params);
        cmdList.textureWriteBarrier(*params.upscaledColor);
    };
}
