#include "BloomNode.h"

#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

BloomNode::BloomNode(Scene& scene)
    : m_scene(scene)
{
}

RenderPipelineNode::ExecuteCallback BloomNode::constructFrame(Registry& reg) const
{
    Texture& mainTexture = *reg.getTexture("SceneColor");
    Extent2D baseExtent = mainTexture.extent();

    const size_t numDownsamples = 6;
    const size_t numLevels = numDownsamples + 1;

    struct {
        std::vector<Texture*> downsampleTextures;
        std::vector<Texture*> upsampleTextures;

        std::vector<BindingSet*> downsampleSets;
        std::vector<BindingSet*> upsampleSets;
    } captures;

    Extent2D extent = baseExtent;
    for (size_t i = 0; i < numLevels; ++i) {
        extent = { extent.width() / 2, extent.height() / 2 };

        Texture& downsampleTex = reg.createTexture2D(extent, Texture::Format::RGBA16F, Texture::Filters::linear(), Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge());
        Texture& upsampleTex = reg.createTexture2D(extent, Texture::Format::RGBA16F, Texture::Filters::linear(), Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge());

        captures.downsampleTextures.push_back(&downsampleTex);
        captures.upsampleTextures.push_back(&upsampleTex);
    }

    for (size_t i = 1; i <= numDownsamples; ++i) {

        // (first iteration: to downsample[1] from downsample[0])
        BindingSet& downsampleSet = reg.createBindingSet({ { 0, ShaderStageCompute, captures.downsampleTextures[i], ShaderBindingType::StorageImage },
                                                           { 1, ShaderStageCompute, captures.downsampleTextures[i - 1], ShaderBindingType::StorageImage } });
        captures.downsampleSets.push_back(&downsampleSet);

        // (first iteration: to upsample[0] from upsample[1] & downsample[0])
        if (i != numDownsamples) {
            BindingSet& upsampleSet = reg.createBindingSet({ { 0, ShaderStageCompute, captures.upsampleTextures[i - 1], ShaderBindingType::StorageImage },
                                                             { 1, ShaderStageCompute, captures.upsampleTextures[i], ShaderBindingType::StorageImage },
                                                             { 2, ShaderStageCompute, captures.downsampleTextures[i - 1], ShaderBindingType::StorageImage } });
            captures.upsampleSets.push_back(&upsampleSet);
        }
    }

    Shader downsampleShader = Shader::createCompute("bloom/downsample.comp");
    ComputeState& downsampleState = reg.createComputeState(downsampleShader, captures.downsampleSets);

    Shader upsampleShader = Shader::createCompute("bloom/upsample.comp");
    ComputeState& upsampleState = reg.createComputeState(upsampleShader, captures.upsampleSets);

    BindingSet& blendBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &mainTexture, ShaderBindingType::StorageImage },
                                                         { 1, ShaderStageCompute, captures.upsampleTextures[0], ShaderBindingType::TextureSampler } });
    Shader bloomBlendShader = Shader::createCompute("bloom/blend.comp");
    ComputeState& bloomBlendComputeState = reg.createComputeState(bloomBlendShader, { &blendBindingSet });

    return [&mainTexture, &downsampleState, &upsampleState, &bloomBlendComputeState, &blendBindingSet, captures](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        static bool enabled = true;
        ImGui::Checkbox("Enabled##bloom", &enabled);

        if (!enabled)
            return;
        
        constexpr Extent3D localSizeForComp { 16, 16, 1 };

        // Copy image to the top level of the downsample stack
        cmdList.copyTexture(mainTexture, *captures.downsampleTextures[0]);

        // Iteratively downsample the stack

        cmdList.setComputeState(downsampleState);
        for (size_t i = 0; i < numDownsamples; ++i) {

            BindingSet& downsampleBindingSet = *captures.downsampleSets[i];
            Texture& targetTexture = *captures.downsampleTextures[i + 1];

            cmdList.bindSet(downsampleBindingSet, 0);
            cmdList.dispatch(targetTexture.extent(), localSizeForComp);
            cmdList.textureWriteBarrier(targetTexture);
        }

        // Copy from (the bottom level of) the downsample stack to the upsample stack

        size_t bottomLevel = numLevels - 1;
        cmdList.copyTexture(*captures.downsampleTextures[bottomLevel], *captures.upsampleTextures[bottomLevel]);

        // Iteratively upsample the stack

        static float upsampleBlurRadius = 0.0036f;
        ImGui::SliderFloat("Upsample blur radius", &upsampleBlurRadius, 0.0f, 0.01f, "%.4f");

        cmdList.setComputeState(upsampleState);
        cmdList.pushConstant(ShaderStageCompute, upsampleBlurRadius, 0);
        for (int i = numDownsamples - 2; i >= 0; --i) {

            BindingSet& upsampleBindingSet = *captures.upsampleSets[i];
            Texture& targetTexture = *captures.upsampleTextures[i];

            cmdList.bindSet(upsampleBindingSet, 0);
            cmdList.dispatch(targetTexture.extent(), localSizeForComp);
            cmdList.textureWriteBarrier(targetTexture);
        }

        // Blend the bloom contribution back into the target texture
        {
            static float bloomBlend = 0.04f;
            ImGui::SliderFloat("Bloom blend", &bloomBlend, 0.0f, 1.0f, "%.6f", ImGuiSliderFlags_Logarithmic);

            cmdList.setComputeState(bloomBlendComputeState);
            cmdList.bindSet(blendBindingSet, 0);
            cmdList.pushConstant(ShaderStageCompute, bloomBlend, 0);
            cmdList.dispatch(mainTexture.extent(), localSizeForComp);
        }
    };
}
