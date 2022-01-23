#include "RTDirectLightNode.h"

RenderPipelineNode::ExecuteCallback RTDirectLightNode::construct(Scene& scene, Registry& reg)
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("RTDirectLight", storageImage);

    BindingSet& rtMeshDataBindingSet = *reg.getBindingSet("SceneRTMeshDataSet");
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), &sceneTLAS },
                                                         { 1, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), reg.getBuffer("SceneCameraData") },
                                                         { 2, ShaderStageRTRayGen, reg.getTexture("SceneEnvironmentMap"), ShaderBindingType::TextureSampler },
                                                         { 3, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage } });

    ShaderFile raygen { "rt-direct-light/raygen.rgen" };
    ShaderFile defaultMissShader { "rt-direct-light/miss.rmiss" };
    ShaderFile shadowMissShader { "rt-direct-light/shadow.rmiss" };
    HitGroup mainHitGroup { ShaderFile("rt-direct-light/default.rchit"), ShaderFile("rt-direct-light/masked.rahit") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { defaultMissShader, shadowMissShader } };

    StateBindings stateDataBindings;
    stateDataBindings.at(0, frameBindingSet);
    stateDataBindings.at(1, rtMeshDataBindingSet);
    stateDataBindings.at(2, materialBindingSet);
    stateDataBindings.at(3, lightBindingSet);

    constexpr uint32_t maxRecursionDepth = 2; // raygen -> closest hit -> shadow ray
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        cmdList.setRayTracingState(rtState);
        cmdList.setNamedUniform("ambientAmount", scene.exposedAmbient());
        cmdList.setNamedUniform("environmentMultiplier", scene.exposedEnvironmentMultiplier());
        cmdList.traceRays(appState.windowExtent());
    };
}
