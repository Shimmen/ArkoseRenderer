#include "PathTracerNode.h"

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"

PathTracerNode::PathTracerNode()
{
}

void PathTracerNode::drawGui()
{
    ImGui::Text("Accumulated frames: %u", m_currentAccumulatedFrames);

    if (ImGui::Button("Reset accumulation")) {
        m_currentAccumulatedFrames = 0;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Accumulation active", &m_shouldAccumulate)) {
        m_currentAccumulatedFrames = 0;
    }

    ImGui::BeginDisabled(!m_shouldAccumulate);
    ImGui::SliderInt("Max accumulated frames", (int*)&m_maxAccumulatedFrames, 1, 5'000);
    ImGui::EndDisabled();
}

RenderPipelineNode::ExecuteCallback PathTracerNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& pathTraceImage = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::RGBA16F);
    Texture& pathTraceAccumImage = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::RGBA32F);
    reg.publish("PathTracerAccumulation", pathTraceAccumImage);

    BindingSet& rtMeshDataBindingSet = *reg.getBindingSet("SceneRTMeshDataSet");
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ ShaderBinding::topLevelAccelerationStructure(sceneTLAS, ShaderStage::RTRayGen | ShaderStage::RTClosestHit),
                                                         ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::AnyRayTrace),
                                                         ShaderBinding::sampledTexture(scene.environmentMapTexture(), ShaderStage::RTRayGen),
                                                         ShaderBinding::storageTexture(pathTraceImage, ShaderStage::RTRayGen) });

    ShaderFile raygen { "pathtracer/pathtracer.rgen" };
    ShaderFile defaultMissShader { "pathtracer/miss.rmiss" };
    ShaderFile shadowMissShader { "pathtracer/shadow.rmiss" };

    ShaderDefine defaultBrdf = ShaderDefine::makeSymbol("PATHTRACER_BRDF_DEFAULT");
    ShaderDefine glassBrdf = ShaderDefine::makeSymbol("PATHTRACER_BRDF_GLASS");

    HitGroup opaqueDefaultBrdfHitGroup { ShaderFile("pathtracer/closesthit.rchit", { defaultBrdf }) };
    HitGroup maskedDefaultBrdfHitGroup { ShaderFile("pathtracer/closesthit.rchit", { defaultBrdf }),
                                         ShaderFile("pathtracer/anyhit.rahit", { defaultBrdf }) };
    HitGroup translucentGlassBrdfHitGroup { ShaderFile("pathtracer/closesthit.rchit", { glassBrdf }) };

    ShaderBindingTable sbt;
    sbt.setRayGenerationShader(raygen);
    sbt.setMissShader(0, defaultMissShader);
    sbt.setMissShader(1, shadowMissShader);
    sbt.setHitGroup(0, opaqueDefaultBrdfHitGroup);
    sbt.setHitGroup(1, maskedDefaultBrdfHitGroup);
    sbt.setHitGroup(2, translucentGlassBrdfHitGroup);

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

        bool imageShouldReset = !m_shouldAccumulate || appState.isRelativeFirstFrame() || scene.camera().hasChangedSinceLastFrame() || scene.hasPendingUploads();
        bool imageShouldAccumulate = !imageShouldReset && m_shouldAccumulate && m_currentAccumulatedFrames < m_maxAccumulatedFrames;

        if (imageShouldReset || imageShouldAccumulate) {
            cmdList.setRayTracingState(rtState);
            cmdList.setNamedUniform("environmentMultiplier", scene.preExposedEnvironmentBrightnessFactor());
            cmdList.setNamedUniform("frameIndex", appState.frameIndex());
            cmdList.traceRays(pipeline().renderResolution());
        }

        if (imageShouldAccumulate) {
            cmdList.setComputeState(accumulateState);
            cmdList.setNamedUniform<uvec2>("targetSize", pathTraceImage.extent().asUIntVector());
            cmdList.setNamedUniform<u32>("frameCount", m_currentAccumulatedFrames);
            cmdList.dispatch(pathTraceImage.extent3D(), { 8, 8, 1 });
            m_currentAccumulatedFrames += 1;
        } else if (imageShouldReset) {
            cmdList.copyTexture(pathTraceImage, pathTraceAccumImage);
            m_currentAccumulatedFrames = 1;
        }
    };
}
