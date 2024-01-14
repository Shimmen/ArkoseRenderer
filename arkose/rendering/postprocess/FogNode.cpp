#include "FogNode.h"

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include <imgui.h>

FogNode::FogNode()
{
}

void FogNode::drawGui()
{
    ImGui::Checkbox("Enabled", &m_enabled);
    ImGui::SliderFloat("Density", &m_fogDensity, 0.0f, 0.75f, "%.6f", ImGuiSliderFlags_Logarithmic);
    ImGui::ColorEdit3("Color", ark::value_ptr(m_fogColor), ImGuiColorEditFlags_NoAlpha);
}

RenderPipelineNode::ExecuteCallback FogNode::construct(GpuScene& scene, Registry& reg)
{
    BindingSet& sceneLightSet = *reg.getBindingSet("SceneLightSet");

    // Collect shadow maps to use
    Texture const& directionalLightShadowMap = reg.getTexture("DirectionalLightShadowMap")
        ? *reg.getTexture("DirectionalLightShadowMap")
        : scene.whiteTexture();

    BindingSet& fogBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(*reg.getTexture("SceneColor"), ShaderStage::Compute),
                                                       ShaderBinding::sampledTexture(*reg.getTexture("SceneDepth"), ShaderStage::Compute),
                                                       ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::Compute),
                                                       ShaderBinding::sampledTexture(directionalLightShadowMap, ShaderStage::Compute) });

    Shader fogShader = Shader::createCompute("postprocess/fog.comp");
    ComputeState& fogState = reg.createComputeState(fogShader, { &fogBindingSet, &sceneLightSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        if (!m_enabled || m_fogDensity < 1e-6f) {
            return;
        }

        cmdList.setComputeState(fogState);
        cmdList.bindSet(fogBindingSet, 0);
        cmdList.bindSet(sceneLightSet, 1);

        Extent2D targetSize = pipeline().renderResolution();
        cmdList.setNamedUniform("targetSize", targetSize);
        cmdList.setNamedUniform("fogDensity", m_fogDensity);
        cmdList.setNamedUniform("fogColor", m_fogColor);

        cmdList.dispatch(targetSize, { 8, 8, 1 });
    };
}
