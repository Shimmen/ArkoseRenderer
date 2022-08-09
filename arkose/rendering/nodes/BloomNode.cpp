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
    m_downsampleSets.clear();
    m_upsampleSets.clear();

    Texture& mainTexture = *reg.getTexture("SceneColor");

    Texture& downsampleTex = reg.createTexture2D(mainTexture.extent(), Texture::Format::RGBA16F, Texture::Filters::linear(), Texture::Mipmap::Linear, Texture::WrapModes::clampAllToEdge());
    downsampleTex.setName("BloomDownsampleTexture");

    Texture& upsampleTex = reg.createTexture2D(mainTexture.extent(), Texture::Format::RGBA16F, Texture::Filters::linear(), Texture::Mipmap::Linear, Texture::WrapModes::clampAllToEdge());
    upsampleTex.setName("BloomUpsampleTexture");

    for (uint32_t i = 1; i < NumMipLevels; ++i) {

        // (first iteration: to downsample[1] from downsample[0])
        BindingSet& downsampleSet = reg.createBindingSet({ ShaderBinding::storageTextureAtMip(downsampleTex, i, ShaderStage::Compute),
                                                           ShaderBinding::storageTextureAtMip(downsampleTex, i - 1, ShaderStage::Compute) });

        // (first iteration: to upsample[0] from upsample[1] & downsample[0])
        BindingSet& upsampleSet = reg.createBindingSet({ ShaderBinding::storageTextureAtMip(upsampleTex, i - 1, ShaderStage::Compute),
                                                         ShaderBinding::storageTextureAtMip(upsampleTex, i, ShaderStage::Compute),
                                                         ShaderBinding::storageTextureAtMip(downsampleTex, i - 1, ShaderStage::Compute) });

        m_downsampleSets.push_back(&downsampleSet);
        m_upsampleSets.push_back(&upsampleSet);
    }

    Shader downsampleShader = Shader::createCompute("bloom/downsample.comp");
    ComputeState& downsampleState = reg.createComputeState(downsampleShader, m_downsampleSets);

    Shader upsampleShader = Shader::createCompute("bloom/upsample.comp");
    ComputeState& upsampleState = reg.createComputeState(upsampleShader, m_upsampleSets);

    BindingSet& blendBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(mainTexture, ShaderStage::Compute),
                                                         ShaderBinding::sampledTexture(upsampleTex, ShaderStage::Compute) });
    Shader bloomBlendShader = Shader::createCompute("bloom/blend.comp");
    ComputeState& bloomBlendComputeState = reg.createComputeState(bloomBlendShader, { &blendBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (!m_enabled)
            return;

        constexpr Extent3D localSizeForComp { 16, 16, 1 };

        // Copy image to the top level of the downsample stack
        cmdList.copyTexture(mainTexture, downsampleTex, 0);

        // Iteratively downsample the stack
        cmdList.setComputeState(downsampleState);
        for (uint32_t targetMip = 1; targetMip < NumMipLevels; ++targetMip) {

            // Only for mip0 -> mip1, apply brightness normalization to prevent fireflies.
            bool applyNormalization = targetMip == 1;
            cmdList.setNamedUniform("applyNormalization", applyNormalization);

            size_t bindingSetIdx = targetMip - 1;
            cmdList.bindSet(*m_downsampleSets[bindingSetIdx], 0);

            cmdList.dispatch(downsampleTex.extentAtMip(targetMip), localSizeForComp);
            cmdList.textureMipWriteBarrier(downsampleTex, targetMip);
        }

        // Copy from (the bottom level of) the downsample stack to the upsample stack
        cmdList.copyTexture(downsampleTex, upsampleTex, BottomMipLevel, BottomMipLevel);

        // Iteratively upsample the stack
        cmdList.setComputeState(upsampleState);
        cmdList.setNamedUniform("blurRadius", m_upsampleBlurRadius);
        for (int targetMip = NumMipLevels - 2; targetMip >= 0; --targetMip) {

            size_t bindingSetIdx = targetMip;
            cmdList.bindSet(*m_upsampleSets[bindingSetIdx], 0);

            cmdList.dispatch(upsampleTex.extentAtMip(targetMip), localSizeForComp);
            cmdList.textureMipWriteBarrier(downsampleTex, targetMip);
        }

        // Blend the bloom contribution back into the target texture
        {
            cmdList.setComputeState(bloomBlendComputeState);
            cmdList.bindSet(blendBindingSet, 0);
            cmdList.pushConstant(ShaderStage::Compute, m_bloomBlend, 0);
            cmdList.dispatch(mainTexture.extent(), localSizeForComp);
        }
    };
}
