#include "GIComposeNode.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

void GIComposeNode::drawGui()
{
    ImGui::Checkbox("Direct light", &m_includeDirectLight);
    ImGui::Checkbox("Glossy indirect (reflections)", &m_includeGlossyGI);
    ImGui::Checkbox("Diffuse indirect (DDGI)", &m_includeDiffuseGI);

    ImGui::Separator();

    ImGui::Checkbox("Include material colors (for indirect)", &m_withMaterialColor);
    ImGui::Checkbox("Include ambient occlusion (for diffuse indirect)", &m_withAmbientOcclusion);
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

        cmdList.setComputeState(giComposeState);
        cmdList.bindSet(composeBindingSet, 0);
        cmdList.bindSet(ddgiSamplingBindingSet, 1);

        cmdList.setNamedUniform("targetSize", sceneColorWithGI.extent());
        cmdList.setNamedUniform("includeDirectLight", m_includeDirectLight);
        cmdList.setNamedUniform("includeDiffuseGI", m_includeDiffuseGI);
        cmdList.setNamedUniform("includeGlossyGI", m_includeGlossyGI);
        cmdList.setNamedUniform("withMaterialColor", m_withMaterialColor);
        cmdList.setNamedUniform("withAmbientOcclusion", m_withAmbientOcclusion);

        cmdList.dispatch({ sceneColorWithGI.extent(), 1 }, { 8, 8, 1 });

        // TODO: Figure out a good way of actually chaining these calls & reusing textures etc.
        cmdList.textureWriteBarrier(sceneColorWithGI);
        cmdList.copyTexture(sceneColorWithGI, sceneColor);

    };
}
