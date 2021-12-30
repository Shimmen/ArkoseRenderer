#include "GIComposeNode.h"

#include "SceneNode.h"
#include "geometry/Frustum.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

std::string GIComposeNode::name()
{
    return "gi-compose";
}

GIComposeNode::GIComposeNode(Scene& scene)
    : RenderGraphNode(GIComposeNode::name())
    , m_scene(scene)
{
}

RenderGraphNode::ExecuteCallback GIComposeNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    Texture& sceneColorBeforeGI = *reg.getTexture("forward", "color").value();
    Texture& baseColorTex = *reg.getTexture("g-buffer", "baseColor").value();
    Texture& ambientOcclusionTex = *reg.getTexture("ssao", "ambient-occlusion").value();
    Texture& diffuseGiTex = *reg.getTexture("forward", "diffuse-gi").value();

    Texture& sceneColorWithGI = reg.createTexture2D(reg.windowRenderTarget().extent(), sceneColorBeforeGI.format(), Texture::Filters::nearest());

    BindingSet& composeBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &sceneColorWithGI, ShaderBindingType::StorageImage },
                                                           { 1, ShaderStageCompute, &sceneColorBeforeGI, ShaderBindingType::TextureSampler },
                                                           { 2, ShaderStageCompute, &baseColorTex, ShaderBindingType::TextureSampler },
                                                           { 3, ShaderStageCompute, &ambientOcclusionTex, ShaderBindingType::TextureSampler },
                                                           { 4, ShaderStageCompute, &diffuseGiTex, ShaderBindingType::TextureSampler } });
    ComputeState& giComposeState = reg.createComputeState(Shader::createCompute("compose/compose-gi.comp"), { &composeBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {

        cmdList.setComputeState(giComposeState);
        cmdList.bindSet(composeBindingSet, 0);

        cmdList.setNamedUniform("targetSize", sceneColorWithGI.extent());

        static bool includeSceneColor = true;
        static bool includeDiffuseGI = true;
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
            IndirectOnly,
            IndirectOnlyNoBaseColor,
        };
        static ComposeMode composeMode = ComposeMode::FullCompose;

        if (ImGui::RadioButton("Full compose", composeMode == ComposeMode::FullCompose)) {
            composeMode = ComposeMode::FullCompose;
            includeSceneColor = true;
            includeDiffuseGI = true;
            withMaterialColor = true;
        }
        if (ImGui::RadioButton("Direct light only", composeMode == ComposeMode::DirectOnly)) {
            composeMode = ComposeMode::DirectOnly;
            includeSceneColor = true;
            includeDiffuseGI = false;
        }
        if (ImGui::RadioButton("Diffuse indirect only", composeMode == ComposeMode::IndirectOnly)) {
            composeMode = ComposeMode::IndirectOnly;
            includeSceneColor = false;
            includeDiffuseGI = true;
            withMaterialColor = true;
        }
        if (ImGui::RadioButton("Diffuse indirect only (ignore material color)", composeMode == ComposeMode::IndirectOnlyNoBaseColor)) {
            composeMode = ComposeMode::IndirectOnlyNoBaseColor;
            includeSceneColor = false;
            includeDiffuseGI = true;
            withMaterialColor = false;
        }
        
        ImGui::Separator();
        ImGui::Checkbox("Include ambient occlusion (for diffuse indirect)", &withAmbientOcclusion);
#endif
        cmdList.setNamedUniform("includeSceneColor", includeSceneColor);
        cmdList.setNamedUniform("includeDiffuseGI", includeDiffuseGI);
        cmdList.setNamedUniform("withMaterialColor", withMaterialColor);
        cmdList.setNamedUniform("withAmbientOcclusion", withAmbientOcclusion);

        cmdList.dispatch({ sceneColorWithGI.extent(), 1 }, { 32, 32, 1 });

        // TODO: Figure out a good way of actually chaining these calls & reusing textures etc.
        cmdList.textureWriteBarrier(sceneColorWithGI);
        cmdList.copyTexture(sceneColorWithGI, sceneColorBeforeGI);
        cmdList.textureWriteBarrier(sceneColorBeforeGI);

    };
}
