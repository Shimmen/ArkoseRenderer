#include "RTVisualisationNode.h"

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"

RTVisualisationNode::RTVisualisationNode(Mode mode)
    : m_mode(mode)
{
}

RenderPipelineNode::ExecuteCallback RTVisualisationNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& storageImage = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::RGBA16F);
    reg.publish("RTVisualisation", storageImage);

    BindingSet& rtMeshDataBindingSet = *reg.getBindingSet("SceneRTMeshDataSet");
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ ShaderBinding::topLevelAccelerationStructure(sceneTLAS, ShaderStage::RTRayGen | ShaderStage::RTClosestHit),
                                                         ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::AnyRayTrace),
                                                         ShaderBinding::sampledTexture(scene.environmentMapTexture(), ShaderStage::RTRayGen),
                                                         ShaderBinding::storageTexture(storageImage, ShaderStage::RTRayGen) });

    bool evaluateDirectLight = m_mode == Mode::DirectLight;
    auto hitGroupDefines = { ShaderDefine::makeBool("RT_EVALUATE_DIRECT_LIGHT", evaluateDirectLight) };

    ShaderFile raygen { "rt-visualisation/raygen.rgen" };
    ShaderFile defaultMissShader { "rayTracing/common/miss.rmiss" };
    ShaderFile shadowMissShader { "rayTracing/common/shadow.rmiss" };
    HitGroup opaqueDefaultBrdfHitGroup { ShaderFile("rayTracing/common/opaque.rchit", hitGroupDefines) };
    HitGroup maskedDefaultBrdfHitGroup { ShaderFile("rayTracing/common/opaque.rchit", hitGroupDefines),
                                         ShaderFile("rayTracing/common/masked.rahit", hitGroupDefines) };

    ShaderBindingTable sbt;
    sbt.setRayGenerationShader(raygen);
    sbt.setMissShader(0, defaultMissShader);
    sbt.setMissShader(1, shadowMissShader);
    sbt.setHitGroup(0, opaqueDefaultBrdfHitGroup);
    sbt.setHitGroup(1, maskedDefaultBrdfHitGroup);
    sbt.setHitGroup(2, opaqueDefaultBrdfHitGroup); // TODO: Make suitable hit group!

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
        cmdList.traceRays(pipeline().renderResolution());
    };
}
