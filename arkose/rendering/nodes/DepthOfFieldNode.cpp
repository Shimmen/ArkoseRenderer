#include "DepthOfFieldNode.h"

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include <imgui.h>

void DepthOfFieldNode::drawGui()
{
    ImGui::Checkbox("Enabled##dof", &m_enabled);

    ImGui::SliderFloat("Max blur size (px)", &m_maxBlurSize, 0.0f, 25.0f);
    ImGui::SliderFloat("Radius scale", &m_radiusScale, 0.1f, 2.0f); // smaller results in nicer quality

    if (ImGui::TreeNode("Debug##fov")) {
        ImGui::Checkbox("Show pixels where blur size is clamped", &m_debugShowClampedBlurSize);
        ImGui::Checkbox("Output circle of confusion visualisation", &m_debugShowCircleOfConfusion);
        ImGui::TreePop();
    }
}

RenderPipelineNode::ExecuteCallback DepthOfFieldNode::construct(GpuScene& scene, Registry& reg)
{
    Buffer& sceneCameraBuffer = *reg.getBuffer("SceneCameraData");
    Texture& sceneColor = *reg.getTexture("SceneColor");
    Texture& sceneDepth= *reg.getTexture("SceneDepth");

    Texture& circleOfConfusionTex = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::R16F);
    Texture& depthOfFieldTex = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::RGBA16F);

    // CoC calculation step
    BindingSet& calculateCocBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(circleOfConfusionTex, ShaderStage::Compute),
                                                                ShaderBinding::sampledTexture(sceneDepth, ShaderStage::Compute),
                                                                ShaderBinding::constantBuffer(sceneCameraBuffer, ShaderStage::Compute) });
    StateBindings calculateCocStateBindings;
    calculateCocStateBindings.at(0, calculateCocBindingSet);
    Shader calculateCocShader = Shader::createCompute("depth-of-field/calculateCoc.comp");
    ComputeState& calculateCocState = reg.createComputeState(calculateCocShader, calculateCocStateBindings);

    // Blur step
    BindingSet& blurBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(depthOfFieldTex, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(circleOfConfusionTex, ShaderStage::Compute),
                                                        ShaderBinding::sampledTexture(sceneColor, ShaderStage::Compute),
                                                        ShaderBinding::constantBuffer(sceneCameraBuffer, ShaderStage::Compute) });
    StateBindings blurStateBindings;
    blurStateBindings.at(0, blurBindingSet);
    Shader blurShader = Shader::createCompute("depth-of-field/bokehBlur.comp");
    ComputeState& blurState = reg.createComputeState(blurShader, blurStateBindings);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (!m_enabled) {
            return;
        }

        Extent2D targetSize = pipeline().renderResolution();
        Camera& camera = scene.scene().camera();

        // Calculate CoC at full resolution
        cmdList.setComputeState(calculateCocState);
        cmdList.setNamedUniform("targetSize", targetSize);
        cmdList.setNamedUniform("focusDepth", camera.focusDepth());
        cmdList.dispatch(targetSize, { 8, 8, 1 });

        cmdList.textureWriteBarrier(circleOfConfusionTex);

        // NOTE: Assuming full-res DoF effect, i.e. same resolution as the camera viewport
        float cocMmToPx = camera.circleOfConfusionMmToPxFactor();

        // Perform blur
        cmdList.setComputeState(blurState);
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