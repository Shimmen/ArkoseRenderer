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
    Texture& sceneColor = *reg.getTexture("SceneColor");
    
    Texture* ambientOcclusionTex = reg.getTexture("AmbientOcclusion");
    if (!ambientOcclusionTex)
        ambientOcclusionTex = &reg.createPixelTexture(vec4(1.0f), false);

    // TODO: Make it optional!
    BindingSet& ddgiSamplingBindingSet = *reg.getBindingSet("DDGISamplingSet");

    Texture* reflectionsTex = reg.getTexture("DenoisedReflections");
    if (!reflectionsTex)
        reflectionsTex = &reg.createPixelTexture(vec4(0.0f), true);

    // TODO: Make it optional
    Texture* reflectionDirectionTex = reg.getTexture("ReflectionDirection");
    ARKOSE_ASSERT(reflectionDirectionTex);

    Texture& sceneColorWithGI = reg.createTexture2D(reg.windowRenderTarget().extent(), sceneColor.format(), Texture::Filters::nearest());

    BindingSet& composeBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::Compute),
                                                           ShaderBinding::storageTexture(sceneColorWithGI, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reg.getTexture("SceneBaseColor"), ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reg.getTexture("SceneMaterial"), ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reg.getTexture("SceneNormalVelocity"), ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reg.getTexture("SceneDepth"), ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(sceneColor, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reflectionsTex, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reflectionDirectionTex, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*ambientOcclusionTex, ShaderStage::Compute) });
    ComputeState& giComposeState = reg.createComputeState(Shader::createCompute("compose/compose-gi.comp"), { &composeBindingSet, &ddgiSamplingBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        bool includeDirectLight = true;
        bool includeDiffuseGI = true;
        bool includeGlossyGI = true;
        bool withMaterialColor = true;

        switch (m_composeMode) {
        case ComposeMode::FullCompose:
            includeDirectLight = true;
            includeDiffuseGI = true;
            includeGlossyGI = true;
            withMaterialColor = true;
            break;
        case ComposeMode::DirectOnly:
            includeDirectLight = true;
            includeDiffuseGI = false;
            includeGlossyGI = false;
            withMaterialColor = true;
            break;
        case ComposeMode::DiffuseIndirectOnly:
            includeDirectLight = false;
            includeDiffuseGI = true;
            includeGlossyGI = false;
            withMaterialColor = true;
            break;
        case ComposeMode::DiffuseIndirectOnlyNoBaseColor:
            includeDirectLight = false;
            includeDiffuseGI = true;
            includeGlossyGI = false;
            withMaterialColor = false;
            break;
        case ComposeMode::GlossyIndirectOnly:
            includeDirectLight = false;
            includeDiffuseGI = false;
            includeGlossyGI = true;
            withMaterialColor = true;
            break;
        }

        cmdList.setComputeState(giComposeState);
        cmdList.bindSet(composeBindingSet, 0);
        cmdList.bindSet(ddgiSamplingBindingSet, 1);

        cmdList.setNamedUniform("targetSize", sceneColorWithGI.extent());
        cmdList.setNamedUniform("includeDirectLight", includeDirectLight);
        cmdList.setNamedUniform("includeDiffuseGI", includeDiffuseGI);
        cmdList.setNamedUniform("includeGlossyGI", includeGlossyGI);
        cmdList.setNamedUniform("withMaterialColor", withMaterialColor);
        cmdList.setNamedUniform("withAmbientOcclusion", m_includeAmbientOcclusion);

        cmdList.dispatch({ sceneColorWithGI.extent(), 1 }, { 8, 8, 1 });

        // TODO: Figure out a good way of actually chaining these calls & reusing textures etc.
        cmdList.textureWriteBarrier(sceneColorWithGI);
        cmdList.copyTexture(sceneColorWithGI, sceneColor);

    };
}
