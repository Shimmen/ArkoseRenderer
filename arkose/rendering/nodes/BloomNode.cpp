#include "BloomNode.h"

#include "utility/Profiling.h"
#include <imgui.h>

void BloomNode::drawGui()
{
    ImGui::Checkbox("Enabled##bloom", &m_enabled);
    ImGui::SliderFloat("Upsample blur radius", &m_upsampleBlurRadius, 0.0f, 0.01f, "%.4f");
    ImGui::SliderFloat("Bloom blend", &m_bloomBlend, 0.0f, 1.0f, "%.6f", ImGuiSliderFlags_Logarithmic);
}

RenderPipelineNode::ExecuteCallback BloomNode::construct(GpuScene& scene, Registry& reg)
{
    m_downsampleStates.clear();
    m_upsampleStates.clear();

    Texture& mainTexture = *reg.getTexture("SceneColor");

    Texture& downsampleTex = reg.createTexture2D(mainTexture.extent(), Texture::Format::RGBA16F, Texture::Filters::linear(), Texture::Mipmap::Linear, ImageWrapModes::clampAllToEdge());
    downsampleTex.setName("BloomDownsampleTexture");

    Texture& upsampleTex = reg.createTexture2D(mainTexture.extent(), Texture::Format::RGBA16F, Texture::Filters::linear(), Texture::Mipmap::Linear, ImageWrapModes::clampAllToEdge());
    upsampleTex.setName("BloomUpsampleTexture");

    Shader downsampleShader = Shader::createCompute("bloom/downsample.comp");
    Shader upsampleShader = Shader::createCompute("bloom/upsample.comp");

    for (uint32_t i = 1; i < NumMipLevels; ++i) {

        // (first iteration: to downsample[1] from downsample[0])
        BindingSet& downsampleSet = reg.createBindingSet({ ShaderBinding::storageTextureAtMip(downsampleTex, i, ShaderStage::Compute),
                                                           ShaderBinding::storageTextureAtMip(downsampleTex, i - 1, ShaderStage::Compute) });

        // (first iteration: to upsample[0] from upsample[1] & downsample[0])
        BindingSet& upsampleSet = reg.createBindingSet({ ShaderBinding::storageTextureAtMip(upsampleTex, i - 1, ShaderStage::Compute),
                                                         ShaderBinding::storageTextureAtMip(upsampleTex, i, ShaderStage::Compute),
                                                         ShaderBinding::storageTextureAtMip(downsampleTex, i - 1, ShaderStage::Compute) });

        StateBindings downsampleStateBindings;
        downsampleStateBindings.at(0, downsampleSet);
        m_downsampleStates.push_back(&reg.createComputeState(downsampleShader, downsampleStateBindings));

        StateBindings upsampleStateBindings;
        upsampleStateBindings.at(0, upsampleSet);
        m_upsampleStates.push_back(&reg.createComputeState(upsampleShader, upsampleStateBindings));
    }

    BindingSet& blendBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(mainTexture, ShaderStage::Compute),
                                                         ShaderBinding::sampledTexture(upsampleTex, ShaderStage::Compute) });
    Shader bloomBlendShader = Shader::createCompute("bloom/blend.comp");
    StateBindings bloomBlendStateBindings;
    bloomBlendStateBindings.at(0, blendBindingSet);
    ComputeState& bloomBlendComputeState = reg.createComputeState(bloomBlendShader, bloomBlendStateBindings);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (!m_enabled) {
            return;
        }

        constexpr Extent3D localSizeForComp { 16, 16, 1 };

        // Copy image to the top level of the downsample stack
        cmdList.copyTexture(mainTexture, downsampleTex, 0);

        // Iteratively downsample the stack
        for (uint32_t targetMip = 1; targetMip < NumMipLevels; ++targetMip) {

            size_t stateIdx = targetMip - 1;
            cmdList.setComputeState(*m_downsampleStates[stateIdx]);

            // Only for mip0 -> mip1, apply brightness normalization to prevent fireflies.
            bool applyNormalization = targetMip == 1;
            cmdList.setNamedUniform("applyNormalization", applyNormalization);

            cmdList.dispatch(downsampleTex.extentAtMip(targetMip), localSizeForComp);
            cmdList.textureMipWriteBarrier(downsampleTex, targetMip);
        }

        // Copy from (the bottom level of) the downsample stack to the upsample stack
        cmdList.copyTexture(downsampleTex, upsampleTex, BottomMipLevel, BottomMipLevel);

        // Iteratively upsample the stack
        for (int targetMip = NumMipLevels - 2; targetMip >= 0; --targetMip) {

            size_t stateIdx = targetMip;
            cmdList.setComputeState(*m_upsampleStates[stateIdx]);

            cmdList.setNamedUniform("blurRadius", m_upsampleBlurRadius);

            cmdList.dispatch(upsampleTex.extentAtMip(targetMip), localSizeForComp);
            cmdList.textureMipWriteBarrier(downsampleTex, targetMip);
        }

        // Blend the bloom contribution back into the target texture
        {
            cmdList.setComputeState(bloomBlendComputeState);
            cmdList.setNamedUniform("bloomBlend", m_bloomBlend);
            cmdList.dispatch(mainTexture.extent(), localSizeForComp);
        }
    };
}
