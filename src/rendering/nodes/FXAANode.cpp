#include "FXAANode.h"

#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

RenderPipelineNode::ExecuteCallback FXAANode::construct(Scene& scene, Registry& reg)
{
    Texture& ldrTexture = *reg.getTexture("SceneColorLDR");
    Texture& replaceTex = reg.createTexture2D(ldrTexture.extent(), ldrTexture.format(), ldrTexture.filters(), ldrTexture.mipmap(), ldrTexture.wrapMode());

    BindingSet& fxaaBindingSet = reg.createBindingSet({ { 0, ShaderStage::Compute, &replaceTex, ShaderBindingType::StorageImage },
                                                        { 1, ShaderStage::Compute, &ldrTexture, ShaderBindingType::TextureSampler } });

    Shader computeShader = Shader::createCompute("fxaa/anti-alias.comp");
    ComputeState& fxaaComputeState = reg.createComputeState(computeShader, { &fxaaBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        static bool enabled = true;
        ImGui::Checkbox("Enabled##fxaa", &enabled);

        static float subpix = 0.75f;
        static float edgeThreshold = 0.166f;
        static float edgeThresholdMin = 0.0833f;
        if (ImGui::TreeNode("Advanced")) {
            ImGui::SliderFloat("Sub-pixel AA", &subpix, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Edge threshold", &edgeThreshold, 0.063f, 0.333f, "%.3f");
            ImGui::SliderFloat("Edge threshold min", &edgeThresholdMin, 0.0312f, 0.0833f, "%.4f");
            ImGui::TreePop();
        }

        if (enabled) {
            cmdList.setComputeState(fxaaComputeState);
            cmdList.bindSet(fxaaBindingSet, 0);

            vec2 pixelSize = vec2(1.0f / ldrTexture.extent().width(), 1.0f / ldrTexture.extent().height());
            cmdList.setNamedUniform("fxaaQualityRcpFrame", pixelSize);
            cmdList.setNamedUniform("fxaaQualitySubpix", subpix);
            cmdList.setNamedUniform("fxaaQualityEdgeThreshold", edgeThreshold);
            cmdList.setNamedUniform("fxaaQualityEdgeThresholdMin", edgeThresholdMin);

            cmdList.dispatch(ldrTexture.extent3D(), { 16, 16, 1 });

            // TODO: This is stupid.. we should have a way to "replace" textures of a name so we can maintain these chains of textures..
            // Well maybe not. Usually we have a compute pass which can have a RW-image for input+output, but the FXAA source doesn't
            // support that, it needs a texture sampler to read from. So this might be quite a special case..
            cmdList.copyTexture(replaceTex, ldrTexture);
        }
    };
}
