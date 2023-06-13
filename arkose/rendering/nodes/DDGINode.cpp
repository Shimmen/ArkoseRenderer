#include "DDGINode.h"

#include "core/Logging.h"
#include "rendering/util/ScopedDebugZone.h"
#include <imgui.h>

// Shader headers
#include "shaders/shared/DDGIData.h"

// Resolutions must be powers of two
static_assert((DDGI_IRRADIANCE_RES & (DDGI_IRRADIANCE_RES - 1)) == 0);
static_assert((DDGI_VISIBILITY_RES & (DDGI_VISIBILITY_RES - 1)) == 0);

// The two different resolutions should be a integer multiplier different
static_assert((DDGI_VISIBILITY_RES % DDGI_IRRADIANCE_RES) == 0 || (DDGI_IRRADIANCE_RES % DDGI_VISIBILITY_RES) == 0);

void DDGINode::drawGui()
{
    ImGui::SliderInt("Rays per probe", &m_raysPerProbeInt, 128, MaxNumProbeSamples);
    ImGui::SliderInt("Probe updates per frame", &m_probeUpdatesPerFrame, 1, MaxNumProbeUpdates);

    ImGui::SliderFloat("Hysteresis (irradiance)", &m_hysteresisIrradiance, 0.85f, 0.98f);
    ImGui::SliderFloat("Hysteresis (visibility)", &m_hysteresisVisibility, 0.85f, 0.98f);

    ImGui::SliderFloat("Visibility sharpness", &m_visibilitySharpness, 1.0f, 100.0f);

    ImGui::Checkbox("Compute probe offsets", &m_computeProbeOffsets);
    ImGui::Checkbox("Apply probe offsets", &m_applyProbeOffsets);

    ImGui::Checkbox("Use scene ambient light", &m_useSceneAmbient);
    if (!m_useSceneAmbient) {
        // todo: make inactive instead of disappear!
        ImGui::SliderFloat("Injected ambient (lx)", &m_injectedAmbientLx, 0.0f, 10'000.0f, "%.0f");
    }
}

RenderPipelineNode::ExecuteCallback DDGINode::construct(GpuScene& scene, Registry& reg)
{
    if (!scene.scene().hasProbeGrid()) {
        ARKOSE_LOG(Error, "DDGINode is used but no probe grid is available, will no-op");
        return RenderPipelineNode::NullExecuteCallback;
    }

    const ProbeGrid& probeGrid = scene.scene().probeGrid();
    DDGIProbeGridData probeGridData { .gridDimensions = ivec4(probeGrid.gridDimensions.asIntVector(), 0),
                                      .probeSpacing = vec4(probeGrid.probeSpacing, 0.0f),
                                      .offsetToFirst = vec4(probeGrid.offsetToFirst, 0.0f) };
    Buffer& probeGridDataBuffer = reg.createBufferForData(probeGridData, Buffer::Usage::ConstantBuffer, Buffer::MemoryHint::GpuOptimal);

    auto irradianceClearColor = ClearColor::dataValues(0, 0, 0, 0);
    Texture& probeAtlasIrradiance = createProbeAtlas(reg, "ddgi-irradiance", probeGrid, irradianceClearColor, Texture::Format::RGBA16F, DDGI_IRRADIANCE_RES, DDGI_ATLAS_PADDING);

    float cameraZFar = scene.camera().zFar();
    auto visibilityClearColor = ClearColor::dataValues(cameraZFar, cameraZFar * cameraZFar, 0, 0);
    Texture& probeAtlasVisibility = createProbeAtlas(reg, "ddgi-visibility", probeGrid, visibilityClearColor, Texture::Format::RG16F, DDGI_VISIBILITY_RES, DDGI_ATLAS_PADDING);

    std::vector<vec3> initialProbeOffsets { static_cast<size_t>(probeGrid.probeCount()), vec3(0.0) };
    Buffer& probeOffsetBuffer = reg.createBuffer(initialProbeOffsets, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);

    BindingSet& ddgiSamplingBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(probeGridDataBuffer),
                                                                ShaderBinding::storageBuffer(probeOffsetBuffer),
                                                                ShaderBinding::sampledTexture(probeAtlasIrradiance),
                                                                ShaderBinding::sampledTexture(probeAtlasVisibility) });
    reg.publish("DDGISamplingSet", ddgiSamplingBindingSet);

    Texture& surfelImage = reg.createTexture2D({ MaxNumProbeUpdates, MaxNumProbeSamples }, Texture::Format::RGBA16F);
    //ARKOSE_LOG(Info, "DDGI surfel size in memory = {}", surfelImage.sizeInMemory());

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ ShaderBinding::topLevelAccelerationStructure(sceneTLAS, ShaderStage::RTRayGen | ShaderStage::RTClosestHit),
                                                         ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::AnyRayTrace),
                                                         ShaderBinding::sampledTexture(scene.environmentMapTexture(), ShaderStage::RTRayGen),
                                                         ShaderBinding::storageTexture(surfelImage, ShaderStage::RTRayGen) });

    auto shaderDefines = { ShaderDefine::makeBool("RT_EVALUATE_DIRECT_LIGHT", true),
                           ShaderDefine::makeBool("RT_USE_EXTENDED_RAY_PAYLOAD", true) };

    ShaderFile raygen { "ddgi/raygen.rgen", shaderDefines };
    ShaderFile defaultMissShader { "rayTracing/common/miss.rmiss" };
    ShaderFile shadowMissShader { "rayTracing/common/shadow.rmiss" };
    HitGroup mainHitGroup { ShaderFile("rayTracing/common/opaque.rchit", shaderDefines),
                            ShaderFile("rayTracing/common/masked.rahit", shaderDefines) };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { defaultMissShader, shadowMissShader } };

    StateBindings rtStateDataBindings;
    rtStateDataBindings.at(0, frameBindingSet);
    rtStateDataBindings.at(1, *reg.getBindingSet("SceneRTMeshDataSet"));
    rtStateDataBindings.at(2, scene.globalMaterialBindingSet());
    rtStateDataBindings.at(3, *reg.getBindingSet("SceneLightSet"));
    rtStateDataBindings.at(4, ddgiSamplingBindingSet);

    constexpr uint32_t maxRecursionDepth = 2; // raygen -> closest/any hit -> shadow ray
    RayTracingState& surfelRayTracingState = reg.createRayTracingState(sbt, rtStateDataBindings, maxRecursionDepth);

    BindingSet& irradianceUpdateBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(surfelImage, ShaderStage::Compute),
                                                                    ShaderBinding::storageTexture(probeAtlasIrradiance, ShaderStage::Compute) });
    ComputeState& irradianceProbeUpdateState = reg.createComputeState(Shader::createCompute("ddgi/probeUpdateIrradiance.comp"), { &irradianceUpdateBindingSet });


    BindingSet& visibilityUpdateBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(surfelImage, ShaderStage::Compute),
                                                                    ShaderBinding::storageTexture(probeAtlasVisibility, ShaderStage::Compute) });
    ComputeState& visibilityProbeUpdateState = reg.createComputeState(Shader::createCompute("ddgi/probeUpdateVisibility.comp"), { &visibilityUpdateBindingSet });

    BindingSet& probeBorderCopyBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(probeAtlasIrradiance, ShaderStage::Compute),
                                                                   ShaderBinding::storageTexture(probeAtlasVisibility, ShaderStage::Compute) });
    ComputeState& probeBorderCopyCornersState = reg.createComputeState(Shader::createCompute("ddgi/probeBorderCopyCorners.comp"), { &probeBorderCopyBindingSet });
    ComputeState& probeBorderCopyIrradianceEdgesState = reg.createComputeState(Shader::createCompute("ddgi/probeBorderCopyEdges.comp", { ShaderDefine::makeInt("TILE_SIZE", DDGI_IRRADIANCE_RES) }), { &probeBorderCopyBindingSet });
    ComputeState& probeBorderCopyVisibilityEdgesState = reg.createComputeState(Shader::createCompute("ddgi/probeBorderCopyEdges.comp", { ShaderDefine::makeInt("TILE_SIZE", DDGI_VISIBILITY_RES) }), { &probeBorderCopyBindingSet });

    BindingSet& probeUpdateOffsetBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(surfelImage, ShaderStage::Compute),
                                                                     ShaderBinding::storageBuffer(probeOffsetBuffer, ShaderStage::Compute) });
    ComputeState& probeMoveComputeState = reg.createComputeState(Shader::createCompute("ddgi/probeUpdateOffset.comp", { ShaderDefine::makeInt("SURFELS_PER_PROBE", MaxNumProbeSamples) }), { &probeUpdateOffsetBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        
        uint32_t frameIdx = appState.frameIndex();
        uint32_t raysPerProbe = static_cast<uint32_t>(m_raysPerProbeInt);
        float ambientLx = m_useSceneAmbient ? scene.scene().ambientIlluminance() : m_injectedAmbientLx;

        uint32_t probeUpdatesThisFrame = std::min(m_probeUpdatesPerFrame, probeGrid.probeCount());
        Extent2D surfelDispatchSize { probeUpdatesThisFrame, raysPerProbe };
        uint32_t firstProbeIdx = m_probeUpdateIdx;

        ivec3 gridDimensions = ivec3(
            scene.scene().probeGrid().gridDimensions.width(),
            scene.scene().probeGrid().gridDimensions.height(),
            scene.scene().probeGrid().gridDimensions.depth());

        vec3 probeSpacing = scene.scene().probeGrid().probeSpacing;
        float gridMaxSpacing = std::max(probeSpacing.x, std::max(probeSpacing.y, probeSpacing.z));

        // 1. Ray trace to collect surfel data (including indirect light from last frame's probe data)
        {
            ScopedDebugZone traceRaysZone(cmdList, "Trace rays");

            cmdList.setRayTracingState(surfelRayTracingState);

            cmdList.setNamedUniform("ambientAmount", ambientLx * scene.lightPreExposure());
            cmdList.setNamedUniform("environmentMultiplier", scene.preExposedEnvironmentBrightnessFactor());
            cmdList.setNamedUniform<float>("parameter1", static_cast<float>(frameIdx));
            cmdList.setNamedUniform<float>("parameter2", static_cast<float>(raysPerProbe));
            cmdList.setNamedUniform<float>("parameter3", static_cast<float>(firstProbeIdx));

            cmdList.traceRays(surfelDispatchSize);
        }

        // 2. Ensure all surfel data has been written
        {
            cmdList.textureWriteBarrier(surfelImage);
        }

        // 3. Update irradiance probes with this frame's new surfel data
        {
            ScopedDebugZone updateIrradianceProbesZone(cmdList, "Update irradiance probes");

            cmdList.setComputeState(irradianceProbeUpdateState);
            cmdList.bindSet(irradianceUpdateBindingSet, 0);

            cmdList.setNamedUniform("hysterisis", appState.isFirstFrame() ? 0.0f : m_hysteresisIrradiance);
            cmdList.setNamedUniform("gridDimensions", gridDimensions);
            cmdList.setNamedUniform("firstProbeIdx", firstProbeIdx);
            cmdList.setNamedUniform("raysPerProbe", raysPerProbe);
            cmdList.setNamedUniform("frameIdx", frameIdx);

            cmdList.dispatch(1, 1, probeUpdatesThisFrame);
        }

        // 4. Update visibility probes with this frame's new surfel data
        {
            ScopedDebugZone updateVisibilityProbesZone(cmdList, "Update visibility probes");

            cmdList.setComputeState(visibilityProbeUpdateState);
            cmdList.bindSet(visibilityUpdateBindingSet, 0);

            cmdList.setNamedUniform("hysterisis", appState.isFirstFrame() ? 0.0f : m_hysteresisVisibility);
            cmdList.setNamedUniform("visibilitySharpness", m_visibilitySharpness);
            cmdList.setNamedUniform("gridDimensions", gridDimensions);
            cmdList.setNamedUniform("gridMaxSpacing", gridMaxSpacing);
            cmdList.setNamedUniform("firstProbeIdx", firstProbeIdx);
            cmdList.setNamedUniform("raysPerProbe", raysPerProbe);
            cmdList.setNamedUniform("frameIdx", frameIdx);

            cmdList.dispatch(1, 1, probeUpdatesThisFrame);
        }

        // 5. Copy probe tile borders
        // TODO: Only update the corners of updated probes!
        {
            ScopedDebugZone copyProbeBordersZone(cmdList, "Copy probe borders");

            // NOTE: We're using y number of xz-sheets layed out across the x-axis.
            // NOTE: We use z=2 since we run two parallel data sets (irradiance & visibility)
            uint32_t probeCountY = gridDimensions.z;
            uint32_t probeCountX = gridDimensions.x * gridDimensions.y;

            // NOTE: No barriers between these: they operate on the same resources but different memory within them, so they can safely overlap!

            {
                ScopedDebugZone copyProbeBordersZone(cmdList, "Copy probe corners");

                cmdList.setComputeState(probeBorderCopyCornersState);
                cmdList.bindSet(probeBorderCopyBindingSet, 0);
                cmdList.dispatch(probeCountX, probeCountY, 2);
            }

            {
                ScopedDebugZone copyProbeEdgesZone(cmdList, "Copy probe edges (irradiance)");

                cmdList.setComputeState(probeBorderCopyIrradianceEdgesState);
                cmdList.bindSet(probeBorderCopyBindingSet, 0);
                cmdList.dispatch(probeCountX, probeCountY, 1);
            }

            {
                ScopedDebugZone copyProbeEdgesZone(cmdList, "Copy probe edges (visibility)");

                cmdList.setComputeState(probeBorderCopyVisibilityEdgesState);
                cmdList.bindSet(probeBorderCopyBindingSet, 0);
                cmdList.dispatch(probeCountX, probeCountY, 1);
            }
        }

        // 6. Move probes away from (static) surfaces and out from backfacing meshes
        if (m_computeProbeOffsets && m_applyProbeOffsets) {
            ScopedDebugZone traceRaysZone(cmdList, "Update probe positions");

            cmdList.setComputeState(probeMoveComputeState);
            cmdList.bindSet(probeUpdateOffsetBindingSet, 0);

            cmdList.setNamedUniform("raysPerProbe", raysPerProbe);
            cmdList.setNamedUniform("frameIdx", frameIdx);
            cmdList.setNamedUniform("deltaTime", appState.deltaTime());
            cmdList.setNamedUniform("probeCount", probeGrid.probeCount());
            cmdList.setNamedUniform("firstProbeIdx", firstProbeIdx);

            float minAxialSpacing = minComponent(probeGrid.probeSpacing);
            float maxProbeOffset = minAxialSpacing / 2.0f;
            cmdList.setNamedUniform("maxOffset", maxProbeOffset);

            // Use a subgroup per probe so we can count backfaces 
            cmdList.dispatch({ probeUpdatesThisFrame, 1, 1 }, { 1, raysPerProbe, 1 });
        } else if (not m_applyProbeOffsets) {
            // TODO: Clear out the offset buffer
        }

        m_probeUpdateIdx = (m_probeUpdateIdx + probeUpdatesThisFrame) % probeGrid.probeCount();
    };
}

Texture& DDGINode::createProbeAtlas(Registry& reg, const std::string& name, const ProbeGrid& probeGrid, const ClearColor& clearColor, Texture::Format format, int probeTileSize, int tileSidePadding) const
{
    ARKOSE_ASSERT(probeTileSize > 0);
    ARKOSE_ASSERT(tileSidePadding >= 0);

    int sizePerTile = tileSidePadding + probeTileSize + tileSidePadding;

    int numTileSheets = probeGrid.gridDimensions.height();
    Extent2D tileSheetExtents { probeGrid.gridDimensions.width() * sizePerTile,
                                probeGrid.gridDimensions.depth() * sizePerTile };
    Extent2D atlasExtents { tileSheetExtents.width() * numTileSheets, tileSheetExtents.height() };
    
    auto [atlasTexture, reuseMode] = reg.createOrReuseTexture2D(name, atlasExtents, format, Texture::Filters::linear(), Texture::Mipmap::None, ImageWrapModes::clampAllToEdge());

    if (reuseMode == Registry::ReuseMode::Created) {
        atlasTexture.clear(clearColor);
    }

    return atlasTexture;
}
