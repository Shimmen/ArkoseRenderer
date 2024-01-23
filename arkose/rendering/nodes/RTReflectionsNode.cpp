#include "RTReflectionsNode.h"

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
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

    Extent2D extent = pipeline().renderResolution();
    Extent2D tileExtent = { (extent.width() + 8 - 1) / 8,
                            (extent.height() + 8 - 1) / 8 };

    m_radianceTex = &reg.createTexture2D(extent, Texture::Format::RGBA16F);
    reg.publish("NoisyReflections", *m_radianceTex);

    // OPTIMIZATION: Use octahedral encoding and RG16F!
    Texture& reflectionDirectionTex = reg.createTexture2D(extent, Texture::Format::RGBA16F);
    reg.publish("ReflectionDirection", reflectionDirectionTex);

    // Ray-traced reflections
    RayTracingState& rtState = createRayTracingState(scene, reg, *m_radianceTex, reflectionDirectionTex, blueNoiseTexture);

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

    ComputeState& historyCopyState = createDenoiserHistoryCopyState(reg);
    ComputeState& reprojectState = createDenoiserReprojectState(reg);
    ComputeState& prefilterState = createDenoiserPrefilterState(reg);
    ComputeState& temporalResolveState = createDenoiserTemporalResolveState(reg);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        // NOTE: Relative first frame includes first frame after e.g. screen resize and other pipline invalidating actions
        const bool firstFrame = appState.isRelativeFirstFrame();

        Extent2D mainExtent = m_radianceTex->extent();
        const Extent3D dispatchLocalSize = { 8, 8, 1 };

        {
            ScopedDebugZone zone { cmdList, "Ray Tracing" };

            cmdList.clearTexture(*m_radianceTex, ClearValue::blackAtMaxDepth());
            cmdList.setRayTracingState(rtState);

            cmdList.setNamedUniform("ambientAmount", scene.preExposedAmbient() + m_injectedAmbient * scene.lightPreExposure());
            cmdList.setNamedUniform("environmentMultiplier", scene.preExposedEnvironmentBrightnessFactor());
            cmdList.setNamedUniform<float>("parameter1", m_mirrorRoughnessThreshold);
            cmdList.setNamedUniform<float>("parameter2", m_noTracingRoughnessThreshold);
            cmdList.setNamedUniform<float>("parameter3", static_cast<float>(appState.frameIndex() % blueNoiseTexture.arrayCount()));

            cmdList.traceRays(mainExtent);
            cmdList.textureWriteBarrier(*m_radianceTex);
        }

        if (m_denoiseEnabled) {

            ScopedDebugZone zone { cmdList, "Denoising" };

            if (firstFrame) {
                // Perform initial copy over to history textures
                cmdList.setComputeState(historyCopyState);
                cmdList.setNamedUniform<bool>("firstCopy", true);
                cmdList.dispatch(mainExtent, dispatchLocalSize);

                // History textures needed for reprojection
                cmdList.textureWriteBarrier(*m_radianceHistoryTex);
                cmdList.textureWriteBarrier(*m_worldSpaceNormalHistoryTex);
                cmdList.textureWriteBarrier(*m_depthRoughnessVarianceNumSamplesHistoryTex);
            }

            auto setNoTracingRoughnessThreshold = [&] {
                cmdList.setNamedUniform("noTracingRoughnessThreshold", m_noTracingRoughnessThreshold);
            };
            auto setTemporalStability = [&] {
                cmdList.setNamedUniform("temporalStability", m_temporalStability);
            };

            cmdList.setComputeState(reprojectState);
            setNoTracingRoughnessThreshold();
            setTemporalStability();
            cmdList.dispatch(mainExtent, dispatchLocalSize);

            cmdList.textureWriteBarrier(*m_varianceTex);
            cmdList.textureWriteBarrier(*m_averageRadianceTex);

            cmdList.setComputeState(prefilterState);
            setNoTracingRoughnessThreshold();
            cmdList.dispatch(mainExtent, dispatchLocalSize);

            cmdList.textureWriteBarrier(*m_numSamplesTex);
            cmdList.textureWriteBarrier(*m_reprojectedRadianceTex);

            cmdList.setComputeState(temporalResolveState);
            setNoTracingRoughnessThreshold();
            setTemporalStability();
            cmdList.dispatch(mainExtent, dispatchLocalSize);

            // Copy over to history textures
            cmdList.setComputeState(historyCopyState);
            cmdList.setNamedUniform<bool>("firstCopy", false);
            cmdList.dispatch(mainExtent, dispatchLocalSize);

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
    BindingSet* ddgiBindingSet = reg.getBindingSet("DDGISamplingSet");

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

    std::vector<ShaderDefine> shaderDefines {};

    StateBindings stateDataBindings;
    stateDataBindings.at(0, frameBindingSet);
    stateDataBindings.at(1, rtMeshDataBindingSet);
    stateDataBindings.at(2, materialBindingSet);
    stateDataBindings.at(3, lightBindingSet);

    if (ddgiBindingSet != nullptr) {
        stateDataBindings.at(4, *ddgiBindingSet);
        shaderDefines.push_back(ShaderDefine::makeBool("WITH_DDGI", true));
        shaderDefines.push_back(ShaderDefine::makeBool("RT_USE_EXTENDED_RAY_PAYLOAD", true));
    }

    ShaderFile raygen { "rt-reflections/raygen.rgen", shaderDefines };
    ShaderFile defaultMissShader { "rayTracing/common/miss.rmiss", shaderDefines };
    ShaderFile shadowMissShader { "rayTracing/common/shadow.rmiss", shaderDefines };
    HitGroup mainHitGroup { ShaderFile("rayTracing/common/opaque.rchit", shaderDefines),
                            ShaderFile("rayTracing/common/masked.rahit", shaderDefines) };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { defaultMissShader, shadowMissShader } };

    constexpr uint32_t maxRecursionDepth = 2; // raygen -> closest hit -> shadow ray
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    return rtState;
}

ComputeState& RTReflectionsNode::createDenoiserHistoryCopyState(Registry& reg)
{
    Shader historyCopyShader = Shader::createCompute("rt-reflections/historyCopy.comp");

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

    StateBindings stateBindings;
    stateBindings.at(0, bindings);

    ComputeState& historyCopyState = reg.createComputeState(historyCopyShader, stateBindings);
    historyCopyState.setName("DenoiserHistoryCopy");

    return historyCopyState;
}

ComputeState& RTReflectionsNode::createDenoiserReprojectState(Registry& reg)
{
    Shader reprojectShader = Shader::createCompute("rt-reflections/reproject.comp");

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

    StateBindings stateBindings;
    stateBindings.at(0, bindings);

    ComputeState& reprojectState = reg.createComputeState(reprojectShader, stateBindings);
    reprojectState.setName("DenoiserReproject");

    return reprojectState;
}

ComputeState& RTReflectionsNode::createDenoiserPrefilterState(Registry& reg)
{
    Shader prefilterShader = Shader::createCompute("rt-reflections/prefilter.comp");

    BindingSet& bindings = reg.createBindingSet({ ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData")),
                                                  ShaderBinding::storageTexture(*m_resolvedRadianceAndVarianceTex),
                                                  ShaderBinding::sampledTexture(*m_radianceTex),
                                                  ShaderBinding::sampledTexture(*m_varianceTex),
                                                  ShaderBinding::sampledTexture(*m_averageRadianceTex),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneDepth")),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneMaterial")),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneNormalVelocity")) });

    StateBindings stateBindings;
    stateBindings.at(0, bindings);

    ComputeState& prefilterState = reg.createComputeState(prefilterShader, stateBindings);
    prefilterState.setName("DenoiserPrefilter");

    return prefilterState;
}

ComputeState& RTReflectionsNode::createDenoiserTemporalResolveState(Registry& reg)
{
    Shader temporalResolveShader = Shader::createCompute("rt-reflections/resolveTemporal.comp");

    BindingSet& bindings = reg.createBindingSet({ ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData")),
                                                  ShaderBinding::storageTexture(*m_temporalAccumulationTex),
                                                  ShaderBinding::sampledTexture(*m_radianceTex),
                                                  ShaderBinding::sampledTexture(*m_varianceTex),
                                                  ShaderBinding::sampledTexture(*m_numSamplesTex),
                                                  ShaderBinding::sampledTexture(*m_averageRadianceTex),
                                                  ShaderBinding::sampledTexture(*m_reprojectedRadianceTex),
                                                  ShaderBinding::sampledTexture(*reg.getTexture("SceneMaterial")) });

    StateBindings stateBindings;
    stateBindings.at(0, bindings);

    ComputeState& temporalResolveState = reg.createComputeState(temporalResolveShader, stateBindings);
    temporalResolveState.setName("DenoiserTemporalResolve");

    return temporalResolveState;
}
