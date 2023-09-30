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
    Texture& normalVelocityTexture = *reg.getTexture("SceneNormalVelocity");
    Texture& materialPropertyTexture = *reg.getTexture("SceneMaterial");
    Texture& baseColorTexture = *reg.getTexture("SceneBaseColor");
    BindingSet& targetBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(colorTexture),
                                                          ShaderBinding::storageTexture(normalVelocityTexture),
                                                          ShaderBinding::storageTexture(materialPropertyTexture),
                                                          ShaderBinding::storageTexture(baseColorTexture) });

    MeshletManager const& meshletManager = scene.meshletManager();
    BindingSet& geometryDataBindingSet = *reg.getBindingSet("VisibilityBufferData");

    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    Texture* dirLightProjectedShadow = reg.getTexture("DirectionalLightProjectedShadow");
    Texture* sphereLightProjectedShadow = reg.getTexture("SphereLightProjectedShadow");
    Texture* localLightShadowMapAtlas = reg.getTexture("LocalLightShadowMapAtlas");
    Buffer* localLightShadowAllocations = reg.getBuffer("LocalLightShadowAllocations");
    if (!dirLightProjectedShadow || !sphereLightProjectedShadow || !localLightShadowMapAtlas || !localLightShadowAllocations) {
        Texture& placeholderTex = reg.createPixelTexture(vec4(1.0f), false);
        Buffer& placeholderBuffer = reg.createBufferForData(std::vector<int>(0), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
        dirLightProjectedShadow = dirLightProjectedShadow ? dirLightProjectedShadow : &placeholderTex;
        sphereLightProjectedShadow = sphereLightProjectedShadow ? sphereLightProjectedShadow : &placeholderTex;
        localLightShadowMapAtlas = localLightShadowMapAtlas ? localLightShadowMapAtlas : &placeholderTex;
        localLightShadowAllocations = localLightShadowAllocations ? localLightShadowAllocations : &placeholderBuffer;
    }
    BindingSet& shadowBindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*dirLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*sphereLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*localLightShadowMapAtlas),
                                                          ShaderBinding::storageBuffer(*localLightShadowAllocations) });

    Shader shader = Shader::createCompute("visibility-buffer/shadeVisibilityBuffer.comp");
    ComputeState& computeState = reg.createComputeState(shader, { &cameraBindingSet, &targetBindingSet, &geometryDataBindingSet, &materialBindingSet, &lightBindingSet, &shadowBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        cmdList.setComputeState(computeState);
        cmdList.bindSet(cameraBindingSet, 0);
        cmdList.bindSet(targetBindingSet, 1);
        cmdList.bindSet(geometryDataBindingSet, 2);
        cmdList.bindSet(materialBindingSet, 3);
        cmdList.bindSet(lightBindingSet, 4);
        cmdList.bindSet(shadowBindingSet, 5);

        cmdList.setNamedUniform("ambientAmount", scene.preExposedAmbient());
        cmdList.setNamedUniform("frustumJitterCorrection", scene.camera().frustumJitterUVCorrection());
        cmdList.setNamedUniform("invTargetSize", colorTexture.extent().inverse());

        // We're dealing with gradients directly in the shader so we actually need to express the mip bias
        // as a factor -- multiplicative instead of additive. Mip levels are calculated from the log2 of the
        // gradient so by applying exp2 to the additive bias we should get something multiplicative and matching!
        float lodBiasGradientFactor = std::exp2f(scene.globalMipBias());
        cmdList.setNamedUniform("mipBias", lodBiasGradientFactor);

        cmdList.dispatch({ appState.windowExtent(), 1 }, { 8, 8, 1 });

    };
}
