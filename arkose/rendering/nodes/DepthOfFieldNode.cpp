#include "DepthOfFieldNode.h"

#include "rendering/scene/GpuScene.h"
#include <imgui.h>

RenderPipelineNode::ExecuteCallback DepthOfFieldNode::construct(GpuScene& scene, Registry& reg)
{
    Buffer& sceneCameraBuffer = *reg.getBuffer("SceneCameraData");
    Texture& sceneColor = *reg.getTexture("SceneColor");
    Texture& sceneDepth= *reg.getTexture("SceneDepth");

    Texture& circleOfConfusionTex = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R16F);
    Texture& depthOfFieldTex = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);

    // CoC calculation step
    BindingSet& calculateCocBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(circleOfConfusionTex, ShaderStage::Compute),
                                                                ShaderBinding::sampledTexture(sceneDepth, ShaderStage::Compute),
                                                                ShaderBinding::constantBuffer(sceneCameraBuffer, ShaderStage::Compute) });
    Shader calculateCocShader = Shader::createCompute("depth-of-field/calculateCoc.comp");
    ComputeState& calculateCocState = reg.createComputeState(calculateCocShader, { &calculateCocBindingSet });

    // Blur step
    BindingSet& blurBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(depthOfFieldTex, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(circleOfConfusionTex, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(sceneColor, ShaderStage::Compute),
                                                        ShaderBinding::constantBuffer(sceneCameraBuffer, ShaderStage::Compute) });
    Shader blurShader = Shader::createCompute("depth-of-field/bokehBlur.comp");
    ComputeState& blurState = reg.createComputeState(blurShader, { &blurBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        ImGui::Checkbox("Enabled##dof", &m_enabled);
        if (!m_enabled)
            return;

        Extent2D targetSize = reg.windowRenderTarget().extent();
        Camera& camera = scene.scene().camera();

        // Calculate CoC at full resolution
        cmdList.setComputeState(calculateCocState);
        cmdList.bindSet(calculateCocBindingSet, 0);
        cmdList.setNamedUniform("targetSize", targetSize);
        cmdList.setNamedUniform("focusDepth", camera.focusDepth());
        cmdList.dispatch(targetSize, { 8, 8, 1 });

        cmdList.textureWriteBarrier(circleOfConfusionTex);

        ImGui::SliderFloat("Max blur size (px)", &m_maxBlurSize, 0.0f, 25.0f);
        ImGui::SliderFloat("Radius scale", &m_radiusScale, 0.1f, 2.0f); // smaller results in nicer quality

        if (ImGui::TreeNode("Debug##fov")) {
            ImGui::Checkbox("Show pixels where blur size is clamped", &m_debugShowClampedBlurSize);
            ImGui::Checkbox("Output circle of confusion visualisation", &m_debugShowCircleOfConfusion);
            ImGui::TreePop();
        }

        // NOTE: Assuming full-res DoF effect, i.e. same resolution as the camera viewport
        float cocMmToPx = camera.circleOfConfusionMmToPxFactor();

        // Perform blur
        cmdList.setComputeState(blurState);
        cmdList.bindSet(blurBindingSet, 0);
        cmdList.setNamedUniform("targetSize", targetSize);
        cmdList.setNamedUniform("circleOfConfusionMmToPx", cocMmToPx);
        cmdList.setNamedUniform("maxBlurSize", m_maxBlurSize);
        cmdList.setNamedUniform("radiusScale", m_radiusScale);
        cmdList.setNamedUniform("debugOutputClampedRadius", m_debugShowClampedBlurSize);
        cmdList.dispatch(targetSize, { 8, 8, 1 });

        // TODO: Don't copy, instead use some smart system to point the next "SceneColor" to this
        cmdList.textureWriteBarrier(depthOfFieldTex);
        cmdList.copyTexture(depthOfFieldTex, sceneColor);

        if (m_debugShowCircleOfConfusion) {
            cmdList.copyTexture(circleOfConfusionTex, sceneColor);
        }
    };
}