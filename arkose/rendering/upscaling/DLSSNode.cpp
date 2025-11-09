#include "DLSSNode.h"

#if WITH_DLSS

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"

bool DLSSNode::isSupported()
{
    return Backend::get().hasDLSSSupport();
}

DLSSNode::DLSSNode(UpscalingQuality quality)
    : m_upscalingQuality(quality)
{
}

void DLSSNode::drawGui()
{
    if (m_dlssFeature == nullptr) {
        return;
    }

    ImGui::Checkbox("Enabled", &m_enabled);

#if 0
    // TODO: Make nice GUI for quality selector! When quality changes, make sure we request the pipeline to reconstruct!
    UpscalingQuality quality = m_dlssFeature->quality();
    if (quality != m_dlssFeature->quality()) {
        m_dlssFeature->setQuality(quality);
    }
#endif

    Extent2D renderRes = pipeline().renderResolution();
    Extent2D outputRes = pipeline().outputResolution();
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

Extent2D DLSSNode::idealRenderResolution(Extent2D outputResolution) const
{
    return Backend::get().queryDLSSRenderResolution(outputResolution, m_upscalingQuality);
}

RenderPipelineNode::ExecuteCallback DLSSNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& sceneColorTex = *reg.getTexture("SceneColor");
    Texture::Description upscaledSceneColorDesc = sceneColorTex.description();
    upscaledSceneColorDesc.extent = pipeline().outputResolution();
    Texture& upscaledSceneColorTex = reg.createTexture(upscaledSceneColorDesc);
    reg.publish("SceneColorUpscaled", upscaledSceneColorTex);

    ExternalFeatureCreateParamsDLSS dlssCreateParams;
    dlssCreateParams.quality = m_upscalingQuality;
    dlssCreateParams.renderResolution = pipeline().renderResolution();
    dlssCreateParams.outputResolution = pipeline().outputResolution();

    m_dlssFeature = &reg.createExternalFeature(ExternalFeatureType::DLSS, &dlssCreateParams);

    ExternalFeatureEvaluateParamsDLSS& params = reg.allocate<ExternalFeatureEvaluateParamsDLSS>();
    params.upscaledColor = &upscaledSceneColorTex;
    params.inputColor = reg.getTexture("SceneColor");
    params.depthTexture = reg.getTexture("SceneDepth");
    params.velocityTexture = reg.getTexture("SceneNormalVelocity");
    params.velocityTextureIsSceneNormalVelocity = true;
    params.exposureTexture = nullptr; // we're not using auto-exposure for now.

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        if (!m_enabled) {
            scene.camera().setFrustumJitteringEnabled(false);
            cmdList.copyTexture(sceneColorTex, upscaledSceneColorTex, ImageFilter::Nearest);
            cmdList.textureWriteBarrier(*params.upscaledColor);
            return;
        }

        scene.camera().setFrustumJitteringEnabled(true);
        if (m_controlGlobalMipBias) {
            float recommendedMipBias = m_dlssFeature->queryParameterF(ExternalFeatureParameter::DLSS_OptimalMipBias);
            scene.setGlobalMipBias(recommendedMipBias);
        }

        params.preExposure = scene.lightPreExposure();
        params.frustumJitterOffset = scene.camera().frustumJitterPixelOffset();

        params.sharpness = m_dlssFeature->queryParameterF(ExternalFeatureParameter::DLSS_OptimalSharpness);

        // TODO: Or camera cut, once we add this kind of functionality..
        params.resetAccumulation = appState.isRelativeFirstFrame();

        cmdList.evaluateExternalFeature(*m_dlssFeature, &params);
        cmdList.textureWriteBarrier(*params.upscaledColor);
    };
}

#endif // WITH_DLSS
