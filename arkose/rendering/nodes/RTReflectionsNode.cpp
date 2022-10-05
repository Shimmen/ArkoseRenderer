#include "RTReflectionsNode.h"

#include "rendering/GpuScene.h"
#include "rendering/util/ScopedDebugZone.h"
#include <imgui.h>

void RTReflectionsNode::drawGui()
{
    ImGui::SliderFloat("Injected ambient (lx)", &m_injectedAmbient, 0.0f, 1'000.0f);

    ImGui::SliderFloat("Perfect mirror threshold", &m_mirrorRoughnessThreshold, 0.0f, m_noTracingRoughnessThreshold - 0.01f);
    ImGui::SliderFloat("No tracing threshold", &m_noTracingRoughnessThreshold, m_mirrorRoughnessThreshold + 0.01f, 1.0f);

    ImGui::Checkbox("Denoise", &m_denoiseEnabled);

    if (ImGui::TreeNode("FidelityFX denoiser settings")) {
        ImGui::SliderFloat("Temporal stability", &m_temporalStability, 0.0f, 1.0f);
        ImGui::TreePop();
    }
}

RenderPipelineNode::ExecuteCallback RTReflectionsNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& blueNoiseTexture = *reg.getTexture("BlueNoise");

    Extent2D extent = reg.windowRenderTarget().extent();
    Extent2D tileExtent = { (extent.width() + 8 - 1) / 8,
                            (extent.height() + 8 - 1) / 8 };

    Texture& reflectionsTex = reg.createTexture2D(extent, Texture::Format::RGBA16F);
    reg.publish("Reflections", reflectionsTex);

    // TODO: Use octahedral encoding and RG16F!
    Texture& reflectionDirectionTex = reg.createTexture2D(extent, Texture::Format::RGBA16F);
    reg.publish("ReflectionDirection", reflectionDirectionTex);

    // TODO: Maybe just keep this name throughout?
    m_radianceTex = &reflectionsTex;

    // Ray-traced reflections
    RayTracingState& rtState = createRayTracingState(scene, reg, reflectionsTex, reflectionDirectionTex, blueNoiseTexture);

    // Denoising

    m_resolvedRadianceAndVarianceTex = &reg.createTexture2D(extent, Texture::Format::RGBA16F);
    reg.publish("DenoisedReflections", *m_resolvedRadianceAndVarianceTex);

    m_reprojectedRadianceTex = &reg.createTexture2D(extent, Texture::Format::RGBA16F);
    m_averageRadianceTex = &reg.createTexture2D(tileExtent, Texture::Format::RGBA16F);
    m_varianceTex = &reg.createTexture2D(extent, Texture::Format::R32F);
    m_numSamplesTex = &reg.createTexture2D(extent, Texture::Format::R32F);

    m_temporalAccumulationTex = &reg.createTexture2D(extent, Texture::Format::RGBA16F);

    m_worldSpaceNormalHistoryTex = &reg.createTexture2D(extent, Texture::Format::RGBA16F); // TODO: alpha not used and can be octahedral packed to RG16F!
    m_radianceHistoryTex = &reg.createTexture2D(extent, Texture::Format::RGBA16F); // TODO: alpha not used
    m_depthRoughnessVarianceNumSamplesHistoryTex = &reg.createTexture2D(extent, Texture::Format::RGBA16F); // TODO: Some of these could be stuffed in the other two history textures!

    DenoiserPassData& historyCopyData = createDenoiserHistoryCopyState(reg);
    DenoiserPassData& reprojectData = createDenoiserReprojectState(reg);
    DenoiserPassData& prefilterData = createDenoiserPrefilterState(reg);
    DenoiserPassData& temporalResolveData = createDenoiserTemporalResolveState(reg);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        // NOTE: Relative first frame includes first frame after e.g. screen resize and other pipline invalidating actions
        const bool firstFrame = appState.isRelativeFirstFrame();

        {
            ScopedDebugZone zone { cmdList, "Ray Tracing" };

            cmdList.clearTexture(reflectionsTex, ClearValue::blackAtMaxDepth());
            cmdList.setRayTracingState(rtState);

            cmdList.setNamedUniform("ambientAmount", m_injectedAmbient * scene.lightPreExposure());
            cmdList.setNamedUniform("environmentMultiplier", scene.preExposedEnvironmentBrightnessFactor());
            cmdList.setNamedUniform<float>("parameter1", m_mirrorRoughnessThreshold);
            cmdList.setNamedUniform<float>("parameter2", m_noTracingRoughnessThreshold);
            cmdList.setNamedUniform<float>("parameter3", static_cast<float>(appState.frameIndex() % blueNoiseTexture.arrayCount()));

            cmdList.traceRays(appState.windowExtent());
            cmdList.textureWriteBarrier(*m_radianceTex);
        }

        if (m_denoiseEnabled)
        {
            ScopedDebugZone zone { cmdList, "Denoising" };

            if (firstFrame) {
                // Perform initial copy over to history textures
                cmdList.setComputeState(historyCopyData.state);
                cmdList.bindSet(historyCopyData.bindings, 0);
                cmdList.setNamedUniform<bool>("firstCopy", true);
                cmdList.dispatch(historyCopyData.globalSize, historyCopyData.localSize); // TODO: Maybe remove from DenoiserPassData type?

                // History textures needed for reprojection
                cmdList.textureWriteBarrier(*m_radianceHistoryTex);
                cmdList.textureWriteBarrier(*m_worldSpaceNormalHistoryTex);
                cmdList.textureWriteBarrier(*m_depthRoughnessVarianceNumSamplesHistoryTex);
            }

            cmdList.setComputeState(reprojectData.state);
            cmdList.bindSet(reprojectData.bindings, 0);
            cmdList.setNamedUniform("noTracingRoughnessThreshold", m_noTracingRoughnessThreshold);
            cmdList.setNamedUniform("temporalStability", m_temporalStability);
            cmdList.dispatch(reprojectData.globalSize, reprojectData.localSize); // TODO: Maybe remove from DenoiserPassData type?

            // TODO ...
            cmdList.textureWriteBarrier(*m_varianceTex);
            cmdList.textureWriteBarrier(*m_averageRadianceTex);

            cmdList.setComputeState(prefilterData.state);
            cmdList.bindSet(prefilterData.bindings, 0);
            cmdList.setNamedUniform("noTracingRoughnessThreshold", m_noTracingRoughnessThreshold);
            cmdList.dispatch(prefilterData.globalSize, prefilterData.localSize); // TODO: Maybe remove from DenoiserPassData type?

            // TODO ...
            //cmdList.textureWriteBarrier(*m_resolvedRadianceAndVarianceTex);
            cmdList.textureWriteBarrier(*m_numSamplesTex);
            cmdList.textureWriteBarrier(*m_reprojectedRadianceTex);

            cmdList.setComputeState(temporalResolveData.state);
            cmdList.bindSet(temporalResolveData.bindings, 0);
            cmdList.setNamedUniform("noTracingRoughnessThreshold", m_noTracingRoughnessThreshold);
            cmdList.setNamedUniform("temporalStability", m_temporalStability);
            cmdList.dispatch(temporalResolveData.globalSize, temporalResolveData.localSize); // TODO: Maybe remove from DenoiserPassData type?

            // Copy over to history textures
            cmdList.setComputeState(historyCopyData.state);
            cmdList.bindSet(historyCopyData.bindings, 0);
            cmdList.setNamedUniform<bool>("firstCopy", false);
            cmdList.dispatch(historyCopyData.globalSize, historyCopyData.localSize); // TODO: Maybe remove from DenoiserPassData type?
        } else {

            // Copy raw results over to the denoised result
            cmdList.copyTexture(*m_radianceTex, *m_resolvedRadianceAndVarianceTex);

        }
    };
}

RayTracingState& RTReflectionsNode::createRayTracingState(GpuScene& scene, Registry& reg, Texture& reflectionsTexture, Texture& reflectionDirectionTex, Texture& blueNoiseTexture) const
{
    BindingSet& rtMeshDataBindingSet = *reg.getBindingSet("SceneRTMeshDataSet");
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ ShaderBinding::topLevelAccelerationStructure(sceneTLAS, ShaderStage::RTRayGen | ShaderStage::RTClosestHit),
                                                         ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::AnyRayTrace),
                                                         ShaderBinding::storageTexture(reflectionsTexture, ShaderStage::RTRayGen),
                                                         ShaderBinding::storageTexture(reflectionDirectionTex, ShaderStage::RTRayGen),
                                                         ShaderBinding::sampledTexture(*reg.getTexture("SceneMaterial"), ShaderStage::RTRayGen),
                                                         ShaderBinding::sampledTexture(*reg.getTexture("SceneNormalVelocity"), ShaderStage::RTRayGen),
                                                         ShaderBinding::sampledTexture(*reg.getTexture("SceneDepth"), ShaderStage::RTRayGen),
                                                         ShaderBinding::sampledTexture(scene.environmentMapTexture(), ShaderStage::RTRayGen),
                                                         ShaderBinding::sampledTexture(blueNoiseTexture, ShaderStage::RTRayGen) });

    ShaderFile raygen { "rt-reflections/raygen.rgen" };
    ShaderFile defaultMissShader { "rayTracing/common/miss.rmiss" };
    ShaderFile shadowMissShader { "rayTracing/common/shadow.rmiss" };
    HitGroup mainHitGroup { ShaderFile("rayTracing/common/opaque.rchit"),
                            ShaderFile("rayTracing/common/masked.rahit") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { defaultMissShader, shadowMissShader } };

    StateBindings stateDataBindings;
    stateDataBindings.at(0, frameBindingSet);
    stateDataBindings.at(1, rtMeshDataBindingSet);
    stateDataBindings.at(2, materialBindingSet);
    stateDataBindings.at(3, lightBindingSet);

    constexpr uint32_t maxRecursionDepth = 2; // raygen -> closest hit -> shadow ray
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    return rtState;
}

RTReflectionsNode::DenoiserPassData& RTReflectionsNode::createDenoiserHistoryCopyState(Registry& reg)
{
    Shader historyCopyShader = Shader::createCompute("rt-reflections/historyCopy.comp");

    Extent2D extent = m_radianceTex->extent(); // todo..

    BindingSet& bindings = reg.createBindingSet({ ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData")),
                                                  ShaderBinding::storageTexture(*m_radianceHistoryTex),
                                                  ShaderBinding::storageTexture(*m_worldSpaceNormalHistoryTex),
                                                  ShaderBinding::storageTexture(*m_depthRoughnessVarianceNumSamplesHistoryTex),
                                                  ShaderBinding::sampledTexture(*m_radianceTex),
                                                  ShaderBinding::sampledTexture(*m_varianceTex),
                                                  ShaderBinding::sampledTexture(*m_numSamplesTex),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneDepth")),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneMaterial")),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneNormalVelocity")) });

    ComputeState& historyCopyState = reg.createComputeState(historyCopyShader, { &bindings });
    historyCopyState.setName("DenoiserHistoryCopy");

    DenoiserPassData& data = reg.allocate<DenoiserPassData>(historyCopyState, bindings,
                                                            Extent3D(extent, 1), Extent3D(8, 8, 1));
    return data;
}

RTReflectionsNode::DenoiserPassData& RTReflectionsNode::createDenoiserReprojectState(Registry& reg)
{
    Shader reprojectShader = Shader::createCompute("rt-reflections/reproject.comp");

    Extent2D extent = m_radianceTex->extent();

    BindingSet& bindings = reg.createBindingSet({ ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData")),
                                                  ShaderBinding::storageTexture(*m_reprojectedRadianceTex),
                                                  ShaderBinding::storageTexture(*m_averageRadianceTex),
                                                  ShaderBinding::storageTexture(*m_varianceTex),
                                                  ShaderBinding::storageTexture(*m_numSamplesTex),
                                                  ShaderBinding::sampledTexture(*m_radianceTex),
                                                  ShaderBinding::sampledTexture(*m_radianceHistoryTex),
                                                  ShaderBinding::sampledTexture(*m_worldSpaceNormalHistoryTex),
                                                  ShaderBinding::sampledTexture(*m_depthRoughnessVarianceNumSamplesHistoryTex),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneDepth")),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneMaterial")),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneNormalVelocity")) });

    ComputeState& reprojectState = reg.createComputeState(reprojectShader, { &bindings });
    reprojectState.setName("DenoiserReproject");

    DenoiserPassData& data = reg.allocate<DenoiserPassData>(reprojectState, bindings,
                                                            Extent3D(extent, 1), Extent3D(8, 8, 1));
    return data;
}

RTReflectionsNode::DenoiserPassData& RTReflectionsNode::createDenoiserPrefilterState(Registry& reg)
{
    Shader prefilterShader = Shader::createCompute("rt-reflections/prefilter.comp");

    Extent2D extent = m_radianceTex->extent();

    BindingSet& bindings = reg.createBindingSet({ ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData")),
                                                  ShaderBinding::storageTexture(*m_resolvedRadianceAndVarianceTex),
                                                  ShaderBinding::sampledTexture(*m_radianceTex),
                                                  ShaderBinding::sampledTexture(*m_varianceTex),
                                                  ShaderBinding::sampledTexture(*m_averageRadianceTex),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneDepth")),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneMaterial")),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneNormalVelocity")) });

    ComputeState& prefilterState = reg.createComputeState(prefilterShader, { &bindings });
    prefilterState.setName("DenoiserPrefilter");

    DenoiserPassData& data = reg.allocate<DenoiserPassData>(prefilterState, bindings,
                                                            Extent3D(extent, 1), Extent3D(8, 8, 1));
    return data;
}

RTReflectionsNode::DenoiserPassData& RTReflectionsNode::createDenoiserTemporalResolveState(Registry& reg)
{
    Shader temporalResolveShader = Shader::createCompute("rt-reflections/resolveTemporal.comp");

    Extent2D extent = m_radianceTex->extent();

    BindingSet& bindings = reg.createBindingSet({ ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData")),
                                                  ShaderBinding::storageTexture(*m_temporalAccumulationTex),
                                                  ShaderBinding::sampledTexture(*m_radianceTex),
                                                  ShaderBinding::sampledTexture(*m_varianceTex),
                                                  ShaderBinding::sampledTexture(*m_numSamplesTex),
                                                  ShaderBinding::sampledTexture(*m_averageRadianceTex),
                                                  ShaderBinding::sampledTexture(*m_reprojectedRadianceTex),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneMaterial")) });

    ComputeState& temporalResolveState = reg.createComputeState(temporalResolveShader, { &bindings });
    temporalResolveState.setName("DenoiserTemporalResolve");

    DenoiserPassData& data = reg.allocate<DenoiserPassData>(temporalResolveState, bindings,
                                                            Extent3D(extent, 1), Extent3D(8, 8, 1));
    return data;
}
