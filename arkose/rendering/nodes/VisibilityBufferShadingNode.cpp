#include "VisibilityBufferShadingNode.h"

#include "rendering/GpuScene.h"
#include <imgui.h>

void VisibilityBufferShadingNode::drawGui()
{
}

RenderPipelineNode::ExecuteCallback VisibilityBufferShadingNode::construct(GpuScene& scene, Registry& reg)
{
    Texture* instanceVisibilityTexture = reg.getTexture("InstanceVisibilityTexture");
    Texture* triangleVisibilityTexture = reg.getTexture("TriangleVisibilityTexture");
    ARKOSE_ASSERT(instanceVisibilityTexture != nullptr && triangleVisibilityTexture != nullptr);

    BindingSet& cameraBindingSet = *reg.getBindingSet("SceneCameraSet");

    Texture& colorTexture = *reg.getTexture("SceneColor");
    Texture& diffuseIrradianceTexture = *reg.getTexture("SceneDiffuseIrradiance");
    Texture& normalVelocityTexture = *reg.getTexture("SceneNormalVelocity");
    Texture& bentNormalTexture = *reg.getTexture("SceneBentNormal");
    Texture& materialPropertyTexture = *reg.getTexture("SceneMaterial");
    Texture& baseColorTexture = *reg.getTexture("SceneBaseColor");
    BindingSet& targetBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(colorTexture),
                                                          ShaderBinding::storageTexture(diffuseIrradianceTexture),
                                                          ShaderBinding::storageTexture(normalVelocityTexture),
                                                          ShaderBinding::storageTexture(bentNormalTexture),
                                                          ShaderBinding::storageTexture(materialPropertyTexture),
                                                          ShaderBinding::storageTexture(baseColorTexture) });

    BindingSet& geometryDataBindingSet = *reg.getBindingSet("VisibilityBufferData");

    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    Texture* dirLightProjectedShadow = reg.getTexture("DirectionalLightProjectedShadow");
    Texture* sphereLightProjectedShadow = reg.getTexture("SphereLightProjectedShadow");
    Texture* localLightShadowMapAtlas = reg.getTexture("LocalLightShadowMapAtlas");
    Buffer* localLightShadowAllocations = reg.getBuffer("LocalLightShadowAllocations");
    if (!dirLightProjectedShadow || !sphereLightProjectedShadow || !localLightShadowMapAtlas || !localLightShadowAllocations) {
        Texture& placeholderTex = reg.createPixelTexture(vec4(1.0f), false);
        Buffer& placeholderBuffer = reg.createBufferForData(std::vector<int>(0), Buffer::Usage::StorageBuffer);
        dirLightProjectedShadow = dirLightProjectedShadow ? dirLightProjectedShadow : &placeholderTex;
        sphereLightProjectedShadow = sphereLightProjectedShadow ? sphereLightProjectedShadow : &placeholderTex;
        localLightShadowMapAtlas = localLightShadowMapAtlas ? localLightShadowMapAtlas : &placeholderTex;
        localLightShadowAllocations = localLightShadowAllocations ? localLightShadowAllocations : &placeholderBuffer;
    }
    BindingSet& shadowBindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*dirLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*sphereLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*localLightShadowMapAtlas),
                                                          ShaderBinding::storageBuffer(*localLightShadowAllocations) });

    StateBindings stateBindings;
    stateBindings.at(0, cameraBindingSet);
    stateBindings.at(1, targetBindingSet);
    stateBindings.at(2, geometryDataBindingSet);
    stateBindings.at(3, materialBindingSet);
    stateBindings.at(4, lightBindingSet);
    stateBindings.at(5, shadowBindingSet);

    Shader shader = Shader::createCompute("visibility-buffer/shadeVisibilityBuffer.comp");
    ComputeState& computeState = reg.createComputeState(shader, stateBindings);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        cmdList.setComputeState(computeState);

        cmdList.setNamedUniform("ambientAmount", scene.preExposedAmbient());
        cmdList.setNamedUniform("frustumJitterCorrection", scene.camera().frustumJitterUVCorrection());
        cmdList.setNamedUniform("invTargetSize", colorTexture.extent().inverse());
        cmdList.setNamedUniform("withMaterialColor", scene.shouldIncludeMaterialColor());

        // We're dealing with gradients directly in the shader so we actually need to express the mip bias
        // as a factor -- multiplicative instead of additive. Mip levels are calculated from the log2 of the
        // gradient so by applying exp2 to the additive bias we should get something multiplicative and matching!
        float lodBiasGradientFactor = std::exp2f(scene.globalMipBias());
        cmdList.setNamedUniform("mipBias", lodBiasGradientFactor);

        cmdList.dispatch({ appState.windowExtent(), 1 }, { 8, 8, 1 });

    };
}
