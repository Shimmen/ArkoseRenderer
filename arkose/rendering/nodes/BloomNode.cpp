#include "BloomNode.h"

#include "utility/Profiling.h"
#include <imgui.h>

RenderPipelineNode::ExecuteCallback BloomNode::construct(GpuScene& scene, Registry& reg)
{
    //m_downsampleTextures.clear();
    //m_upsampleTextures.clear();
    m_downsampleSets.clear();
    m_upsampleSets.clear();

    Texture& mainTexture = *reg.getTexture("SceneColor");
    //Extent2D baseExtent = mainTexture.extent();

    Texture& downsampleTex = reg.createTexture2D(mainTexture.extent(), Texture::Format::RGBA16F, Texture::Filters::linear(), Texture::Mipmap::Linear, Texture::WrapModes::clampAllToEdge());
    downsampleTex.setName("BloomDownsampleTexture");

    Texture& upsampleTex = reg.createTexture2D(mainTexture.extent(), Texture::Format::RGBA16F, Texture::Filters::linear(), Texture::Mipmap::Linear, Texture::WrapModes::clampAllToEdge());
    upsampleTex.setName("BloomUpsampleTexture");

    /*
    Extent2D extent = baseExtent;
    for (size_t i = 0; i < NumMipLevels; ++i) {
        extent = { extent.width() / 2, extent.height() / 2 };

        Texture& downsampleTex = reg.createTexture2D(extent, Texture::Format::RGBA16F, Texture::Filters::linear(), Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge());
        Texture& upsampleTex = reg.createTexture2D(extent, Texture::Format::RGBA16F, Texture::Filters::linear(), Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge());

        m_downsampleTextures.push_back(&downsampleTex);
        m_upsampleTextures.push_back(&upsampleTex);
    }
    */

    for (size_t i = 1; i < NumMipLevels; ++i) {

        // (first iteration: to downsample[1] from downsample[0])
        //BindingSet& downsampleSet = reg.createBindingSet({ ShaderBinding::storageTexture(*m_downsampleTextures[i], ShaderStage::Compute),
        //                                                   ShaderBinding::storageTexture(*m_downsampleTextures[i - 1], ShaderStage::Compute) });
        BindingSet& downsampleSet = reg.createBindingSet({ ShaderBinding::storageTextureAtMip(downsampleTex, i, ShaderStage::Compute),
                                                           ShaderBinding::storageTextureAtMip(downsampleTex, i - 1, ShaderStage::Compute) });
        m_downsampleSets.push_back(&downsampleSet);

        // (first iteration: to upsample[0] from upsample[1] & downsample[0])
        //bool isLastMip = i == BottomMipLevel; // NumMipLevels - 1;
        //if (isLastMip == false) {
            //BindingSet& upsampleSet = reg.createBindingSet({ ShaderBinding::storageTexture(*m_upsampleTextures[i - 1], ShaderStage::Compute),
            //                                                 ShaderBinding::storageTexture(*m_upsampleTextures[i], ShaderStage::Compute),
            //                                                 ShaderBinding::storageTexture(*m_downsampleTextures[i - 1], ShaderStage::Compute) });
            BindingSet& upsampleSet = reg.createBindingSet({ ShaderBinding::storageTextureAtMip(upsampleTex, i - 1, ShaderStage::Compute),
                                                             ShaderBinding::storageTextureAtMip(upsampleTex, i, ShaderStage::Compute),
                                                             ShaderBinding::storageTextureAtMip(downsampleTex, i - 1, ShaderStage::Compute) });
            m_upsampleSets.push_back(&upsampleSet);
        //}
    }

    Shader downsampleShader = Shader::createCompute("bloom/downsample.comp");
    ComputeState& downsampleState = reg.createComputeState(downsampleShader, m_downsampleSets);

    Shader upsampleShader = Shader::createCompute("bloom/upsample.comp");
    ComputeState& upsampleState = reg.createComputeState(upsampleShader, m_upsampleSets);

    BindingSet& blendBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(mainTexture, ShaderStage::Compute),
                                                         //ShaderBinding::sampledTexture(*m_upsampleTextures[0], ShaderStage::Compute) });
                                                         ShaderBinding::sampledTexture(upsampleTex, ShaderStage::Compute) });
    Shader bloomBlendShader = Shader::createCompute("bloom/blend.comp");
    ComputeState& bloomBlendComputeState = reg.createComputeState(bloomBlendShader, { &blendBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        static bool enabled = true;
        ImGui::Checkbox("Enabled##bloom", &enabled);

        if (!enabled)
            return;
        
        constexpr Extent3D localSizeForComp { 16, 16, 1 };

        // Copy image to the top level of the downsample stack
        //cmdList.copyTexture(mainTexture, *m_downsampleTextures[0]);
        cmdList.copyTexture(mainTexture, downsampleTex, 0);

        // Iteratively downsample the stack

        cmdList.setComputeState(downsampleState);
        for (size_t targetMip = 1; targetMip < NumMipLevels; ++targetMip) {

            size_t bindingSetIdx = targetMip - 1;
            BindingSet& downsampleBindingSet = *m_downsampleSets[bindingSetIdx];
            //Texture& targetTexture = *m_downsampleTextures[i + 1]; // TODO: We need this for the size only now! Maybe we can have a function Texture::extentForMip(i)?

            cmdList.bindSet(downsampleBindingSet, 0);

            //cmdList.dispatch(targetTexture.extent(), localSizeForComp);
            cmdList.dispatch(downsampleTex.extentAtMip(targetMip), localSizeForComp);

            //cmdList.textureWriteBarrier(targetTexture);
            cmdList.textureMipWriteBarrier(downsampleTex, targetMip);
        }

        // Copy from (the bottom level of) the downsample stack to the upsample stack

        
        //cmdList.copyTexture(*m_downsampleTextures[bottomLevel], *m_upsampleTextures[bottomLevel]);
        //cmdList.copyTexture(downsampleTex, *m_upsampleTextures[bottomLevel], bottomLevel, 0);
        cmdList.copyTexture(downsampleTex, upsampleTex, BottomMipLevel, BottomMipLevel);

        // Iteratively upsample the stack

        static float upsampleBlurRadius = 0.0036f;
        ImGui::SliderFloat("Upsample blur radius", &upsampleBlurRadius, 0.0f, 0.01f, "%.4f");

        cmdList.setComputeState(upsampleState);
        cmdList.pushConstant(ShaderStage::Compute, upsampleBlurRadius, 0);
        for (int targetMip = NumMipLevels - 2; targetMip >= 0; --targetMip) {

            size_t bindingSetIdx = targetMip;
            BindingSet& upsampleBindingSet = *m_upsampleSets[bindingSetIdx];
            //Texture& targetTexture = *m_upsampleTextures[i];

            cmdList.bindSet(upsampleBindingSet, 0);

            //cmdList.dispatch(targetTexture.extent(), localSizeForComp);
            cmdList.dispatch(upsampleTex.extentAtMip(targetMip), localSizeForComp);

            //cmdList.textureWriteBarrier(targetTexture);
            cmdList.textureMipWriteBarrier(downsampleTex, targetMip);
        }

        // Blend the bloom contribution back into the target texture
        {
            static float bloomBlend = 0.04f;
            ImGui::SliderFloat("Bloom blend", &bloomBlend, 0.0f, 1.0f, "%.6f", ImGuiSliderFlags_Logarithmic);

            cmdList.setComputeState(bloomBlendComputeState);
            cmdList.bindSet(blendBindingSet, 0);
            cmdList.pushConstant(ShaderStage::Compute, bloomBlend, 0);
            cmdList.dispatch(mainTexture.extent(), localSizeForComp);
        }
    };
}
