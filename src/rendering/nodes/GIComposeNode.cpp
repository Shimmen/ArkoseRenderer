#include "GIComposeNode.h"

#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

RenderPipelineNode::ExecuteCallback GIComposeNode::construct(Scene& scene, Registry& reg)
{
    Texture& sceneColorBeforeGI = *reg.getTexture("SceneColor");
    Texture& baseColorTex = *reg.getTexture("SceneBaseColor");
    Texture& ambientOcclusionTex = *reg.getTexture("AmbientOcclusion");
    Texture& diffuseGiTex = *reg.getTexture("DiffuseGI");
    Texture& reflectionsTex = *reg.getTexture("Reflections");

    Texture& sceneColorWithGI = reg.createTexture2D(reg.windowRenderTarget().extent(), sceneColorBeforeGI.format(), Texture::Filters::nearest());

    BindingSet& composeBindingSet = reg.createBindingSet({ { 0, ShaderStage::Compute, &sceneColorWithGI, ShaderBindingType::StorageImage },
                                                           { 1, ShaderStage::Compute, &sceneColorBeforeGI, ShaderBindingType::TextureSampler },
                                                           { 2, ShaderStage::Compute, &baseColorTex, ShaderBindingType::TextureSampler },
                                                           { 3, ShaderStage::Compute, &ambientOcclusionTex, ShaderBindingType::TextureSampler },
                                                           { 4, ShaderStage::Compute, &diffuseGiTex, ShaderBindingType::TextureSampler },
                                                           { 5, ShaderStage::Compute, &reflectionsTex, ShaderBindingType::TextureSampler } });
    ComputeState& giComposeState = reg.createComputeState(Shader::createCompute("compose/compose-gi.comp"), { &composeBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        cmdList.setComputeState(giComposeState);
        cmdList.bindSet(composeBindingSet, 0);

        cmdList.setNamedUniform("targetSize", sceneColorWithGI.extent());

        static bool includeSceneColor = true;
        static bool includeDiffuseGI = true;
        static bool includeGlossyGI = true;
        static bool withMaterialColor = true;
        static bool withAmbientOcclusion = true;
#if 0
        ImGui::Checkbox("Include scene color", &includeSceneColor);
        ImGui::Checkbox("Include diffuse GI", &includeDiffuseGI);
        if (includeDiffuseGI) {
            ImGui::Checkbox("... with material color", &withMaterialColor);
            ImGui::Checkbox("... with ambient occlusion", &withAmbientOcclusion);
        }
#else
        enum class ComposeMode {
            FullCompose,
            DirectOnly,
            DiffuseIndirectOnly,
            DiffuseIndirectOnlyNoBaseColor,
            GlossyIndirectOnly,
        };
        static ComposeMode composeMode = ComposeMode::FullCompose;

        if (ImGui::RadioButton("Full compose", composeMode == ComposeMode::FullCompose)) {
            composeMode = ComposeMode::FullCompose;
            includeSceneColor = true;
            includeDiffuseGI = true;
            includeGlossyGI = true;
            withMaterialColor = true;
        }
        if (ImGui::RadioButton("Direct light only", composeMode == ComposeMode::DirectOnly)) {
            composeMode = ComposeMode::DirectOnly;
            includeSceneColor = true;
            includeDiffuseGI = false;
            includeGlossyGI = false;
        }
        if (ImGui::RadioButton("Diffuse indirect only", composeMode == ComposeMode::DiffuseIndirectOnly)) {
            composeMode = ComposeMode::DiffuseIndirectOnly;
            includeSceneColor = false;
            includeDiffuseGI = true;
            includeGlossyGI = false;
            withMaterialColor = true;
        }
        if (ImGui::RadioButton("Diffuse indirect only (ignore material color)", composeMode == ComposeMode::DiffuseIndirectOnlyNoBaseColor)) {
            composeMode = ComposeMode::DiffuseIndirectOnlyNoBaseColor;
            includeSceneColor = false;
            includeDiffuseGI = true;
            includeGlossyGI = false;
            withMaterialColor = false;
        }
        if (ImGui::RadioButton("Glossy indirect only", composeMode == ComposeMode::GlossyIndirectOnly)) {
            composeMode = ComposeMode::GlossyIndirectOnly;
            includeSceneColor = false;
            includeDiffuseGI = false;
            includeGlossyGI = true;
            withMaterialColor = true;
        }
        
        ImGui::Separator();
        ImGui::Checkbox("Include ambient occlusion (for diffuse indirect)", &withAmbientOcclusion);
#endif
        cmdList.setNamedUniform("includeSceneColor", includeSceneColor);
        cmdList.setNamedUniform("includeDiffuseGI", includeDiffuseGI);
        cmdList.setNamedUniform("includeGlossyGI", includeGlossyGI);
        cmdList.setNamedUniform("withMaterialColor", withMaterialColor);
        cmdList.setNamedUniform("withAmbientOcclusion", withAmbientOcclusion);

        cmdList.dispatch({ sceneColorWithGI.extent(), 1 }, { 32, 32, 1 });

        // TODO: Figure out a good way of actually chaining these calls & reusing textures etc.
        cmdList.textureWriteBarrier(sceneColorWithGI);
        cmdList.copyTexture(sceneColorWithGI, sceneColorBeforeGI);
        cmdList.textureWriteBarrier(sceneColorBeforeGI);

    };
}
