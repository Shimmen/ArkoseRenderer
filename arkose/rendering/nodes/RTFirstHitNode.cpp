#include "RTFirstHitNode.h"

#include "rendering/scene/GpuScene.h"

RenderPipelineNode::ExecuteCallback RTFirstHitNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("RTFirstHit", storageImage);

    BindingSet& environmentBindingSet = reg.createBindingSet({ { 0, ShaderStage::RTMiss, &scene.environmentMapTexture(), ShaderBindingType::SampledTexture } });
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& rtMeshDataBindingSet = *reg.getBindingSet("SceneRTMeshDataSet");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ ShaderBinding::topLevelAccelerationStructure(sceneTLAS, ShaderStage::RTRayGen),
                                                         ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::RTRayGen),
                                                         ShaderBinding::storageTexture(storageImage, ShaderStage::RTRayGen) });

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
