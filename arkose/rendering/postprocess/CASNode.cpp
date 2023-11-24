#include "CASNode.h"

#include "rendering/RenderPipeline.h"
#include <imgui.h>

CASNode::CASNode(std::string textureName)
    : m_textureName(std::move(textureName))
{
}

void CASNode::drawGui()
{
    ImGui::Checkbox("Enabled", &m_enabled);
    ImGui::SliderFloat("Sharpness", &m_sharpness, 0.0f, 1.0f);
}

RenderPipelineNode::ExecuteCallback CASNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& inputColorTex = *reg.getTexture(m_textureName);
    Texture& sharpenedTex = reg.createTexture2D(pipeline().renderResolution(), inputColorTex.format());

    BindingSet& casBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(sharpenedTex, ShaderStage::Compute),
                                                       ShaderBinding::sampledTexture(inputColorTex, ShaderStage::Compute) });

    Shader casShader = Shader::createCompute("cas/cas.comp");
    ComputeState& casState = reg.createComputeState(casShader, { &casBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        if (!m_enabled) {
            return;
        }

        cmdList.setComputeState(casState);
        cmdList.bindSet(casBindingSet, 0);

        Extent2D targetSize = pipeline().renderResolution();
        cmdList.setNamedUniform("sharpness", m_sharpness);
        cmdList.setNamedUniform("targetSize", targetSize);

        cmdList.dispatch(targetSize, { 8, 8, 1 });

        // TODO: Don't copy, instead use some smart system to point the next `inputColorTex` to this
        cmdList.textureWriteBarrier(sharpenedTex);
        cmdList.copyTexture(sharpenedTex, inputColorTex);
    };
}
