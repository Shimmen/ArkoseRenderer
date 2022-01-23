#include "RTFirstHitNode.h"

RenderPipelineNode::ExecuteCallback RTFirstHitNode::construct(Scene& scene, Registry& reg)
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("RTFirstHit", storageImage);

    BindingSet& environmentBindingSet = reg.createBindingSet({ { 0, ShaderStageRTMiss, reg.getTexture("SceneEnvironmentMap"), ShaderBindingType::TextureSampler } });
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& rtMeshDataBindingSet = *reg.getBindingSet("SceneRTMeshDataSet");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, &sceneTLAS },
                                                         { 1, ShaderStageRTRayGen, reg.getBuffer("SceneCameraData") },
                                                         { 2, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage } });

    ShaderFile raygen = ShaderFile("rt-firsthit/raygen.rgen");
    HitGroup mainHitGroup { ShaderFile("rt-firsthit/closestHit.rchit") };
    ShaderFile missShader { ShaderFile("rt-firsthit/miss.rmiss") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { missShader } };

    StateBindings stateBindings;
    stateBindings.at(0, frameBindingSet);
    stateBindings.at(1, rtMeshDataBindingSet);
    stateBindings.at(2, materialBindingSet);
    stateBindings.at(3, environmentBindingSet);

    uint32_t maxRecursionDepth = 1;
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateBindings, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        cmdList.setRayTracingState(rtState);
        cmdList.traceRays(appState.windowExtent());
    };
}
