#include "LightingComposeNode.h"

#include "core/Logging.h"
#include "rendering/RenderPipeline.h"
#include "utility/Profiling.h"
#include <imgui.h>

void LightingComposeNode::drawGui()
{
    // TODO: This could be a really nice 2-column-checkbox layout where you select specular/diffuse for direct/indirect!

    ImGui::Checkbox("Direct light (+emissive)", &m_includeDirectLight);
    ImGui::Checkbox("Skin diffuse light", &m_includeSkinDiffuseLight);

    ImGui::Separator();

    ImGui::Checkbox("Glossy indirect (reflections)", &m_includeGlossyGI);

    ImGui::Separator();

    ImGui::Checkbox("Diffuse indirect (DDGI)", &m_includeDiffuseGI);

    if (!m_includeDiffuseGI) { 
        ImGui::BeginDisabled();
    }

    ImGui::Checkbox("Include baked occlusion", &m_withBakedOcclusion);

    ImGui::Checkbox("Use bent normal direction (if available)", &m_useBentNormalDirection);

    if (!m_useBentNormalDirection) { ImGui::BeginDisabled(); }
    ImGui::Checkbox("Include bent normal occlusion", &m_withBentNormalOcclusion);
    if (!m_useBentNormalDirection) { ImGui::EndDisabled(); }

    if (!m_hasScreenSpaceOcclusionTexture) { ImGui::BeginDisabled(); }
    ImGui::Checkbox("Include screen space occlusion", &m_withScreenSpaceOcclusion);
    if (!m_hasScreenSpaceOcclusionTexture) { ImGui::EndDisabled(); }

    if (!m_includeDiffuseGI) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    ImGui::Checkbox("Include material colors", &m_scene->shouldIncludeMaterialColorMutable());
}

RenderPipelineNode::ExecuteCallback LightingComposeNode::construct(GpuScene& scene, Registry& reg)
{
    m_scene = &scene;

    Texture& sceneColor = *reg.getTexture("SceneColor");
    Texture& sceneDiffuseIrradiance = *reg.getTexture("SceneDiffuseIrradiance");

    Texture* screenSpaceOcclusionTex = reg.getTexture("AmbientOcclusion");
    if (screenSpaceOcclusionTex) {
        m_hasScreenSpaceOcclusionTexture = true;
    } else {
        // this won't be sampled, it's just so we can put something in the binding set
        screenSpaceOcclusionTex = &reg.createPixelTexture(vec4(0.0f), false);
        m_hasScreenSpaceOcclusionTexture = false;
        m_withScreenSpaceOcclusion = false;
    }

    Texture* reflectionsTex = reg.getTexture("DenoisedReflections");
    Texture* reflectionDirectionTex = reg.getTexture("ReflectionDirection");
    if (!reflectionsTex || !reflectionDirectionTex) {
        Texture& blackTex = reg.createPixelTexture(vec4(0.0f), true);
        reflectionsTex = &blackTex;
        reflectionDirectionTex = &blackTex;
    }

    m_ddgiBindingSet = reg.getBindingSet("DDGISamplingSet");

    Texture& sceneColorWithGI = reg.createTexture2D(pipeline().renderResolution(), sceneColor.format(), Texture::Filters::nearest());

    BindingSet& composeBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::Compute),
                                                           ShaderBinding::storageTexture(sceneColorWithGI, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reg.getTexture("SceneBaseColor"), ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reg.getTexture("SceneMaterial"), ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reg.getTexture("SceneNormalVelocity"), ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reg.getTexture("SceneBentNormal"), ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reg.getTexture("SceneDepth"), ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(sceneColor, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(sceneDiffuseIrradiance, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reflectionsTex, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*reflectionDirectionTex, ShaderStage::Compute),
                                                           ShaderBinding::sampledTexture(*screenSpaceOcclusionTex, ShaderStage::Compute) });

    StateBindings stateBindings;
    stateBindings.at(0, composeBindingSet);
    if (m_ddgiBindingSet != nullptr) {
        stateBindings.at(1, *m_ddgiBindingSet);
    }

    std::vector<ShaderDefine> shaderDefines {};
    shaderDefines.push_back(ShaderDefine::makeBool("WITH_DDGI", m_ddgiBindingSet != nullptr));

    Shader composeShader = Shader::createCompute("lighting/lightingCompose.comp", shaderDefines);
    ComputeState& giComposeState = reg.createComputeState(composeShader, stateBindings);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        cmdList.setComputeState(giComposeState);

        cmdList.setNamedUniform("targetSize", sceneColorWithGI.extent());
        cmdList.setNamedUniform("includeDirectLight", m_includeDirectLight);
        cmdList.setNamedUniform("includeSkinDiffuseLight", m_includeSkinDiffuseLight);
        cmdList.setNamedUniform("includeDiffuseGI", m_includeDiffuseGI);
        cmdList.setNamedUniform("withBakedOcclusion", m_withBakedOcclusion);
        cmdList.setNamedUniform("useBentNormalDirection", m_useBentNormalDirection);
        cmdList.setNamedUniform("withBentNormalOcclusion", m_withBentNormalOcclusion);
        cmdList.setNamedUniform("withScreenSpaceOcclusion", m_withScreenSpaceOcclusion);
        cmdList.setNamedUniform("includeGlossyGI", m_includeGlossyGI);
        cmdList.setNamedUniform("withMaterialColor", scene.shouldIncludeMaterialColor());

        cmdList.dispatch({ sceneColorWithGI.extent(), 1 }, { 8, 8, 1 });

        // TODO: Figure out a good way of actually chaining these calls & reusing textures etc.
        cmdList.textureWriteBarrier(sceneColorWithGI);
        cmdList.copyTexture(sceneColorWithGI, sceneColor, ImageFilter::Nearest);

    };
}
