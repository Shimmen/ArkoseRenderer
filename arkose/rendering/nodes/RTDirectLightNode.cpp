#include "RTDirectLightNode.h"

RenderPipelineNode::ExecuteCallback RTDirectLightNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("RTDirectLight", storageImage);

    BindingSet& rtMeshDataBindingSet = *reg.getBindingSet("SceneRTMeshDataSet");
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStage::RTRayGen | ShaderStage::RTClosestHit, &sceneTLAS },
                                                         { 1, ShaderStage::RTRayGen | ShaderStage::RTClosestHit, reg.getBuffer("SceneCameraData") },
                                                         { 2, ShaderStage::RTRayGen, &scene.environmentMapTexture(), ShaderBindingType::TextureSampler },
                                                         { 3, ShaderStage::RTRayGen, &storageImage, ShaderBindingType::StorageTexture } });

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
        cmdList.setNamedUniform("ambientAmount", scene.preExposedAmbient());
        cmdList.setNamedUniform("environmentMultiplier", scene.preExposedEnvironmentBrightnessFactor());
        cmdList.traceRays(appState.windowExtent());
    };
}
