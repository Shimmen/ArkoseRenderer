#include "PathTracerNode.h"

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"

PathTracerNode::PathTracerNode()
{
}

void PathTracerNode::drawGui()
{
    ImGui::Text("Accumulated frames: %u", m_accumulatedFrames);
}

RenderPipelineNode::ExecuteCallback PathTracerNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& pathTraceImage = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::RGBA16F);
    Texture& pathTraceAccumImage = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::RGBA32F);
    reg.publish("PathTracerAccumulation", pathTraceAccumImage);

    BindingSet& rtMeshDataBindingSet = *reg.getBindingSet("SceneRTMeshDataSet");
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    Texture& blueNoiseTexture = *reg.getTexture("BlueNoise");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ ShaderBinding::topLevelAccelerationStructure(sceneTLAS, ShaderStage::RTRayGen | ShaderStage::RTClosestHit),
                                                         ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::AnyRayTrace),
                                                         ShaderBinding::sampledTexture(scene.environmentMapTexture(), ShaderStage::RTRayGen),
                                                         ShaderBinding::sampledTexture(blueNoiseTexture, ShaderStage::RTClosestHit),
                                                         ShaderBinding::storageTexture(pathTraceImage, ShaderStage::RTRayGen) });

    ShaderFile raygen { "pathtracer/pathtracer.rgen" };
    ShaderFile defaultMissShader { "pathtracer/miss.rmiss" };
    ShaderFile shadowMissShader { "pathtracer/shadow.rmiss" };
    HitGroup mainHitGroup { ShaderFile("pathtracer/opaque.rchit"),
                            ShaderFile("pathtracer/masked.rahit") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { defaultMissShader, shadowMissShader } };

    StateBindings stateDataBindings;
    stateDataBindings.at(0, frameBindingSet);
    stateDataBindings.at(1, rtMeshDataBindingSet);
    stateDataBindings.at(2, materialBindingSet);
    stateDataBindings.at(3, lightBindingSet);

    constexpr uint32_t maxRecursionDepth = 2; // raygen -> closest/any hit -> shadow/miss
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    BindingSet& accumBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(pathTraceAccumImage, ShaderStage::Compute),
                                                         ShaderBinding::storageTexture(pathTraceImage, ShaderStage::Compute) });

    StateBindings accumulateStateBindings;
    accumulateStateBindings.at(0, accumBindingSet);

    Shader accumulateShader = Shader::createCompute("pathtracer/accumulate.comp");
    ComputeState& accumulateState = reg.createComputeState(accumulateShader, accumulateStateBindings);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        bool imageShouldReset = appState.isRelativeFirstFrame() || scene.camera().hasChangedSinceLastFrame() || scene.hasPendingUploads();
        bool imageShouldAccumulate = !imageShouldReset && m_accumulatedFrames < 256;

        if (imageShouldReset || imageShouldAccumulate) {
            cmdList.setRayTracingState(rtState);
            cmdList.setNamedUniform("environmentMultiplier", scene.preExposedEnvironmentBrightnessFactor());
            // TODO: We want to accumulate more frames than the array count and we need a constant source of new (blue) noise
            cmdList.setNamedUniform("blueNoiseLayerIndex", appState.frameIndex());// % blueNoiseTexture.arrayCount());
            cmdList.traceRays(appState.windowExtent());
        }

        if (imageShouldAccumulate) {
            cmdList.setComputeState(accumulateState);
            cmdList.setNamedUniform<uvec2>("targetSize", pathTraceImage.extent().asUIntVector());
            cmdList.setNamedUniform<u32>("frameCount", m_accumulatedFrames);
            cmdList.dispatch(pathTraceImage.extent3D(), { 8, 8, 1 });
            m_accumulatedFrames += 1;
        } else if (imageShouldReset) {
            cmdList.copyTexture(pathTraceImage, pathTraceAccumImage);
            m_accumulatedFrames = 1;
        }
    };
}
