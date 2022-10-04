#include "GIComposeNode.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

void GIComposeNode::drawGui()
{
    if (ImGui::RadioButton("Full compose", m_composeMode == ComposeMode::FullCompose)) {
        m_composeMode = ComposeMode::FullCompose;
    }
    if (ImGui::RadioButton("Direct light only", m_composeMode == ComposeMode::DirectOnly)) {
        m_composeMode = ComposeMode::DirectOnly;
    }
    if (ImGui::RadioButton("Diffuse indirect only", m_composeMode == ComposeMode::DiffuseIndirectOnly)) {
        m_composeMode = ComposeMode::DiffuseIndirectOnly;
    }
    if (ImGui::RadioButton("Diffuse indirect only (ignore material color)", m_composeMode == ComposeMode::DiffuseIndirectOnlyNoBaseColor)) {
        m_composeMode = ComposeMode::DiffuseIndirectOnlyNoBaseColor;
    }
    if (ImGui::RadioButton("Glossy indirect only", m_composeMode == ComposeMode::GlossyIndirectOnly)) {
        m_composeMode = ComposeMode::GlossyIndirectOnly;
    }

    ImGui::Separator();

    ImGui::Checkbox("Include ambient occlusion (for diffuse indirect)", &m_includeAmbientOcclusion);
}

RenderPipelineNode::ExecuteCallback GIComposeNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& sceneColorBeforeGI = *reg.getTexture("SceneColor");
    Texture& baseColorTex = *reg.getTexture("SceneBaseColor");
    Texture& ambientOcclusionTex = *reg.getTexture("AmbientOcclusion");
    Texture& diffuseGiTex = *reg.getTexture("DiffuseGI");

    Texture* reflectionsTex = reg.getTexture("DenoisedReflections");
    if (!reflectionsTex)
        reflectionsTex = &reg.createPixelTexture(vec4(0.0f), true);

    Texture& sceneColorWithGI = reg.createTexture2D(reg.windowRenderTarget().extent(), sceneColorBeforeGI.format(), Texture::Filters::nearest());

    BindingSet& composeBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(sceneColorWithGI, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(sceneColorBeforeGI, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(baseColorTex, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(ambientOcclusionTex, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(diffuseGiTex, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reflectionsTex, ShaderStage::Compute) });
    ComputeState& giComposeState = reg.createComputeState(Shader::createCompute("compose/compose-gi.comp"), { &composeBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        bool includeSceneColor = true;
        bool includeDiffuseGI = true;
        bool includeGlossyGI = true;
        bool withMaterialColor = true;

        switch (m_composeMode) {
        case ComposeMode::FullCompose:
            includeSceneColor = true;
            includeDiffuseGI = true;
            includeGlossyGI = true;
            withMaterialColor = true;
            break;
        case ComposeMode::DirectOnly:
            includeSceneColor = true;
            includeDiffuseGI = false;
            includeGlossyGI = false;
            withMaterialColor = true;
            break;
        case ComposeMode::DiffuseIndirectOnly:
            includeSceneColor = false;
            includeDiffuseGI = true;
            includeGlossyGI = false;
            withMaterialColor = true;
            break;
        case ComposeMode::DiffuseIndirectOnlyNoBaseColor:
            includeSceneColor = false;
            includeDiffuseGI = true;
            includeGlossyGI = false;
            withMaterialColor = false;
            break;
        case ComposeMode::GlossyIndirectOnly:
            includeSceneColor = false;
            includeDiffuseGI = false;
            includeGlossyGI = true;
            withMaterialColor = true;
            break;
        }

        cmdList.setComputeState(giComposeState);
        cmdList.bindSet(composeBindingSet, 0);

        cmdList.setNamedUniform("targetSize", sceneColorWithGI.extent());
        cmdList.setNamedUniform("includeSceneColor", includeSceneColor);
        cmdList.setNamedUniform("includeDiffuseGI", includeDiffuseGI);
        cmdList.setNamedUniform("includeGlossyGI", includeGlossyGI);
        cmdList.setNamedUniform("withMaterialColor", withMaterialColor);
        cmdList.setNamedUniform("withAmbientOcclusion", m_includeAmbientOcclusion);

        cmdList.dispatch({ sceneColorWithGI.extent(), 1 }, { 32, 32, 1 });

        // TODO: Figure out a good way of actually chaining these calls & reusing textures etc.
        cmdList.textureWriteBarrier(sceneColorWithGI);
        cmdList.copyTexture(sceneColorWithGI, sceneColorBeforeGI);
        cmdList.textureWriteBarrier(sceneColorBeforeGI);

    };
}
