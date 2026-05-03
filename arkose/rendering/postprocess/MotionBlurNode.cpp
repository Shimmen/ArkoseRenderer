#include "MotionBlurNode.h"

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include "rendering/backend/base/Texture.h"
#include "rendering/backend/shader/Shader.h"
#include <algorithm>
#include <imgui.h>

void MotionBlurNode::drawGui()
{
    ImGui::Checkbox("Enabled", &m_enabled);

    ImGui::SliderInt("Max blur radius (px)", reinterpret_cast<int*>(&m_maxBlurRadiusPixels), 4, TileSize);
    ImGui::SliderInt("Sample count", reinterpret_cast<int*>(&m_sampleCount), 1, 33);
    ImGui::SliderFloat("Soft Z extent (m)", &m_softZExtent, 1e-3f, 0.1f, "%.3f", ImGuiSliderFlags_Logarithmic);
}

RenderPipelineNode::ExecuteCallback MotionBlurNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& sceneColorTex = *reg.getTexture("SceneColor");
    Texture& sceneNormalVelocityTex = *reg.getTexture("SceneNormalVelocity");
    Texture& sceneDepthTex = *reg.getTexture("SceneDepth");

    Extent2D tileExtent = Extent2D::divideAndRoundDownClampTo1(sceneColorTex.extent(), TileSize);
    Texture& tileMaxTex = reg.createTexture2D(tileExtent, Texture::Format::RG16F, Texture::Filters::nearest());
    Texture& neighborMaxTex = reg.createTexture2D(tileExtent, Texture::Format::RG16F, Texture::Filters::nearest());
    Texture& motionBlurResultTex = reg.createTexture2D(sceneColorTex.extent(), sceneColorTex.format());

    BindingSet& tileMaxBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(tileMaxTex, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(sceneNormalVelocityTex, ShaderStage::Compute) });

    BindingSet& neighborMaxBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(neighborMaxTex, ShaderStage::Compute),
                                                               ShaderBinding::sampledTexture(tileMaxTex, ShaderStage::Compute) });

    BindingSet& motionBlurBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(motionBlurResultTex, ShaderStage::Compute),
                                                              ShaderBinding::sampledTexture(sceneColorTex, ShaderStage::Compute),
                                                              ShaderBinding::sampledTexture(sceneNormalVelocityTex, ShaderStage::Compute),
                                                              ShaderBinding::sampledTexture(sceneDepthTex, ShaderStage::Compute),
                                                              ShaderBinding::sampledTexture(neighborMaxTex, ShaderStage::Compute) });

    std::vector<ShaderDefine> shaderDefines = { ShaderDefine::makeInt("TILE_SIZE", TileSize) };

    StateBindings tileMaxStateBindings;
    tileMaxStateBindings.at(0, tileMaxBindingSet);
    ComputeState& tileMaxState = reg.createComputeState(Shader::createCompute("motion-blur/tileMax.comp", shaderDefines), tileMaxStateBindings);
    StateBindings neighborMaxStateBindings;
    neighborMaxStateBindings.at(0, neighborMaxBindingSet);
    ComputeState& neighborMaxState = reg.createComputeState(Shader::createCompute("motion-blur/neighborMax.comp", shaderDefines), neighborMaxStateBindings);

    StateBindings motionBlurStateBindings;
    motionBlurStateBindings.at(0, motionBlurBindingSet);
    motionBlurStateBindings.at(1, *reg.getBindingSet("SceneCameraSet"));
    ComputeState& motionBlurState = reg.createComputeState(Shader::createCompute("motion-blur/motionBlur.comp", shaderDefines), motionBlurStateBindings);
    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer&) {
        if (!m_enabled) {
            return;
        }

        Camera const& camera = scene.camera();

        float shutterScale;
        if (camera.useManualShutterSpeedForMotionBlur()) {
            shutterScale = camera.shutterSpeed() / appState.deltaTime();
        } else {
            shutterScale = camera.motionBlurShutterAngle() / 360.0f;
        }

        if (shutterScale <= 0.01f) {
            return;
        }

        Extent2D velocityTexSize = sceneColorTex.extent();
        Extent2D tileTexSize = tileMaxTex.extent();

        //
        // Calculate max velocity in tile
        //

        cmdList.setComputeState(tileMaxState);
        cmdList.setNamedUniform("velocityTexSize", velocityTexSize);
        cmdList.setNamedUniform("tileTexSize", tileTexSize);
        cmdList.dispatch(tileMaxTex.extent(), { 8, 8, 1 });
        cmdList.textureWriteBarrier(tileMaxTex);

        //
        // Expand max velocity to neighbor tiles
        //

        cmdList.setComputeState(neighborMaxState);
        cmdList.setNamedUniform("tileTexSize", tileTexSize);
        cmdList.dispatch(tileMaxTex.extent(), { 8, 8, 1 });
        cmdList.textureWriteBarrier(neighborMaxTex);

        //
        // Calculate motion blur
        //

        cmdList.setComputeState(motionBlurState);

        // Sample count should be odd, to ensure there is a center sample
        u32 sampleCount = m_sampleCount | 1;

        cmdList.setNamedUniform("shutterScale", shutterScale);
        cmdList.setNamedUniform("maxBlurRadiusPixels", m_maxBlurRadiusPixels);
        cmdList.setNamedUniform("sampleCount", sampleCount);
        cmdList.setNamedUniform("softZExtent", m_softZExtent);
        cmdList.setNamedUniform("targetSize", velocityTexSize);
        cmdList.setNamedUniform("frameIndex", appState.frameIndex());

        cmdList.dispatch(sceneColorTex.extent(), { 8, 8, 1 });
        cmdList.textureWriteBarrier(motionBlurResultTex);

        cmdList.copyTexture(motionBlurResultTex, sceneColorTex, ImageFilter::Nearest);
    };
}
