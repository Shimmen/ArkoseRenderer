#include "DDGINode.h"

#include "rendering/util/ScopedDebugZone.h"
#include <imgui.h>

// Shader headers
#include "RTData.h"
#include "DDGIData.h"

// Resolutions must be powers of two
static_assert((DDGI_IRRADIANCE_RES & (DDGI_IRRADIANCE_RES - 1)) == 0);
static_assert((DDGI_VISIBILITY_RES & (DDGI_VISIBILITY_RES - 1)) == 0);

// The two different resolutions should be a integer multiplier different
static_assert((DDGI_VISIBILITY_RES % DDGI_IRRADIANCE_RES) == 0 || (DDGI_IRRADIANCE_RES % DDGI_VISIBILITY_RES) == 0);

DDGINode::DDGINode(Scene& scene)
    : RenderGraphNode(DDGINode::name())
    , m_scene(scene)
{
}

std::string DDGINode::name()
{
    return "ddgi";
}

void DDGINode::constructNode(Registry& reg)
{
    if (!m_scene.hasProbeGrid()) {
        LogError("DDGINode is used but no probe grid is available, will no-op");
        return;
    }

    DDGIProbeGridData probeGridData { .gridDimensions = ivec4(m_scene.probeGrid().gridDimensions.asIntVector(), 0),
                                      .probeSpacing = vec4(m_scene.probeGrid().probeSpacing, 0.0f),
                                      .offsetToFirst = vec4(m_scene.probeGrid().offsetToFirst, 0.0f) };
    m_probeGridDataBuffer = &reg.createBufferForData(probeGridData, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::GpuOptimal);

    m_objectDataBindingSet = createMeshDataBindingSet(reg);

    auto irradianceClearColor = ClearColor::dataValues(0, 0, 0, 0);
    m_probeAtlasIrradiance = createProbeAtlas(reg, "ddgi-irradiance", m_scene.probeGrid(), irradianceClearColor, Texture::Format::RGBA16F, DDGI_IRRADIANCE_RES, DDGI_ATLAS_PADDING);

    float cameraZFar = m_scene.camera().zFar;
    auto visibilityClearColor = ClearColor::dataValues(cameraZFar, cameraZFar * cameraZFar, 0, 0);
    m_probeAtlasVisibility = createProbeAtlas(reg, "ddgi-visibility", m_scene.probeGrid(), visibilityClearColor, Texture::Format::RG16F, DDGI_VISIBILITY_RES, DDGI_ATLAS_PADDING);

    BindingSet& ddgiSamplingBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, m_probeGridDataBuffer },
                                                                { 1, ShaderStageFragment, m_probeAtlasIrradiance, ShaderBindingType::TextureSampler },
                                                                { 2, ShaderStageFragment, m_probeAtlasVisibility, ShaderBindingType::TextureSampler } });
    ddgiSamplingBindingSet.setName("DDGISampling");
    reg.publish("ddgi-sampling-set", ddgiSamplingBindingSet);
}

RenderGraphNode::ExecuteCallback DDGINode::constructFrame(Registry& reg) const
{
    if (!m_scene.hasProbeGrid())
        return {};

    const int probeCount = m_scene.probeGrid().probeCount(); // TODO: maybe don't expect to be able to update all in one surfel image?
    static constexpr int maxNumProbeSamples = 128; // we dynamically choose to do fewer samples but not more since it's the fixed image size now
    Texture& surfelImage = reg.createTexture2D({ probeCount, maxNumProbeSamples }, Texture::Format::RGBA16F);

    #define USE_DEBUG_TARGET 0

#if USE_DEBUG_TARGET
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("target", storageImage);
#endif

    TopLevelAS& sceneTLAS = m_scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), &sceneTLAS },
                                                         { 1, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), reg.getBuffer("camera") },
                                                         { 2, ShaderStageRTRayGen, m_probeGridDataBuffer },
                                                         { 3, ShaderStageRTRayGen, reg.getTexture("scene", "environmentMap"), ShaderBindingType::TextureSampler },
#if USE_DEBUG_TARGET
                                                         { 4, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage } });
#else
                                                         { 4, ShaderStageRTRayGen, &surfelImage, ShaderBindingType::StorageImage } });
#endif

    ShaderFile raygen { "ddgi/raygen.rgen" };
    ShaderFile defaultMissShader { "ddgi/miss.rmiss" };
    ShaderFile shadowMissShader { "ddgi/shadow.rmiss" };
    HitGroup mainHitGroup { ShaderFile("ddgi/default.rchit"), ShaderFile("ddgi/masked.rahit") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { defaultMissShader, shadowMissShader } };

    StateBindings rtStateDataBindings;
    rtStateDataBindings.at(0, frameBindingSet);
    rtStateDataBindings.at(1, *m_objectDataBindingSet);
    rtStateDataBindings.at(2, m_scene.globalMaterialBindingSet());
    rtStateDataBindings.at(3, *reg.getBindingSet("lightSet"));

    constexpr uint32_t maxRecursionDepth = 2; // raygen -> closest/any hit -> shadow ray
    RayTracingState& surfelRayTracingState = reg.createRayTracingState(sbt, rtStateDataBindings, maxRecursionDepth);

    BindingSet& irradianceUpdateBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &surfelImage, ShaderBindingType::StorageImage },
                                                                    { 1, ShaderStageCompute, m_probeAtlasIrradiance, ShaderBindingType::StorageImage } });
    ComputeState& irradianceProbeUpdateState = reg.createComputeState(Shader::createCompute("ddgi/probeUpdateIrradiance.comp"), { &irradianceUpdateBindingSet });


    BindingSet& visibilityUpdateBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &surfelImage, ShaderBindingType::StorageImage },
                                                                    { 1, ShaderStageCompute, m_probeAtlasVisibility, ShaderBindingType::StorageImage } });
    ComputeState& visibilityProbeUpdateState = reg.createComputeState(Shader::createCompute("ddgi/probeUpdateVisibility.comp"), { &visibilityUpdateBindingSet });

    BindingSet& probeBorderCopyBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, m_probeAtlasIrradiance, ShaderBindingType::StorageImage },
                                                                   { 1, ShaderStageCompute, m_probeAtlasVisibility, ShaderBindingType::StorageImage } });
    ComputeState& probeBorderCopyCornersState = reg.createComputeState(Shader::createCompute("ddgi/probeBorderCopyCorners.comp"), { &probeBorderCopyBindingSet });
    ComputeState& probeBorderCopyIrradianceEdgesState = reg.createComputeState(Shader::createCompute("ddgi/probeBorderCopyEdges.comp", { ShaderDefine::makeInt("TILE_SIZE", DDGI_IRRADIANCE_RES) }), { &probeBorderCopyBindingSet });
    ComputeState& probeBorderCopyVisibilityEdgesState = reg.createComputeState(Shader::createCompute("ddgi/probeBorderCopyEdges.comp", { ShaderDefine::makeInt("TILE_SIZE", DDGI_VISIBILITY_RES) }), { &probeBorderCopyBindingSet });

    return [&, probeCount](const AppState& appState, CommandList& cmdList) {
        static int raysPerProbeInt = maxNumProbeSamples;
        ImGui::SliderInt("Rays per probe", &raysPerProbeInt, 1, maxNumProbeSamples);
        uint32_t raysPerProbe = static_cast<uint32_t>(raysPerProbeInt);

        static float hysteresisIrradiance = 0.98f;
        static float hysteresisVisibility = 0.98f;
        ImGui::SliderFloat("Hysteresis (irradiance)", &hysteresisIrradiance, 0.85f, 0.98f);
        ImGui::SliderFloat("Hysteresis (visibility)", &hysteresisVisibility, 0.85f, 0.98f);

        // TODO: What's a good default?!
        static float visibilitySharpness = 5.0f;
        ImGui::SliderFloat("Visibility sharpness", &visibilitySharpness, 1.0f, 10.0f);

        float ambientLx = m_scene.ambient();
        static bool useSceneAmbient = true;
        ImGui::Checkbox("Use scene ambient light", &useSceneAmbient);
        if (!useSceneAmbient) {
            static float injectedAmbientLx = 100.0f;
            ImGui::SliderFloat("Injected ambient (lx)", &injectedAmbientLx, 0.0f, 10'000.0f, "%.0f");
            ambientLx = injectedAmbientLx;
        }

        uint32_t frameIdx = appState.frameIndex();

        ivec3 gridDimensions = ivec3(
            m_scene.probeGrid().gridDimensions.width(),
            m_scene.probeGrid().gridDimensions.height(),
            m_scene.probeGrid().gridDimensions.depth());

        // 1. Ray trace to collect surfel data (including indirect light from last frame's probe data)
        {
            ScopedDebugZone traceRaysZone(cmdList, "Trace rays");

            cmdList.setRayTracingState(surfelRayTracingState);

            cmdList.setNamedUniform("ambientAmount", ambientLx * m_scene.lightPreExposureValue());
            cmdList.setNamedUniform("environmentMultiplier", m_scene.exposedEnvironmentMultiplier());
            cmdList.setNamedUniform("frameIdx", frameIdx);

#if USE_DEBUG_TARGET
            cmdList.traceRays(appState.windowExtent());
#else
            cmdList.traceRays(surfelImage.extent());
#endif
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

            cmdList.setNamedUniform("hysterisis", appState.isFirstFrame() ? 0.0f : hysteresisIrradiance);
            cmdList.setNamedUniform("gridDimensions", gridDimensions);
            cmdList.setNamedUniform("raysPerProbe", raysPerProbe);
            cmdList.setNamedUniform("frameIdx", frameIdx);

            cmdList.dispatch(1, 1, probeCount);
        }

        // 4. Update visibility probes with this frame's new surfel data
        {
            ScopedDebugZone updateVisibilityProbesZone(cmdList, "Update visibility probes");

            cmdList.setComputeState(visibilityProbeUpdateState);
            cmdList.bindSet(visibilityUpdateBindingSet, 0);

            cmdList.setNamedUniform("hysterisis", appState.isFirstFrame() ? 0.0f : hysteresisVisibility);
            cmdList.setNamedUniform("visibilitySharpness", visibilitySharpness);
            cmdList.setNamedUniform("gridDimensions", gridDimensions);
            cmdList.setNamedUniform("raysPerProbe", raysPerProbe);
            cmdList.setNamedUniform("frameIdx", frameIdx);

            cmdList.dispatch(1, 1, probeCount);
        }

        // 5. Copy probe tile borders
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

        // 6. Ensure all probes have been updated
        {
            // TODO: The render graph should take care of this!
            cmdList.textureWriteBarrier(*m_probeAtlasIrradiance);
            cmdList.textureWriteBarrier(*m_probeAtlasVisibility);
        }
        
    };
}

BindingSet* DDGINode::createMeshDataBindingSet(Registry& reg) const
{
    const VertexLayout vertexLayout = { VertexComponent::Normal3F,
                                        VertexComponent::TexCoord2F };

    std::vector<RTTriangleMesh> rtMeshes {};
    m_scene.forEachMesh([&](size_t meshIdx, Mesh& mesh) {
        const DrawCallDescription& drawCallDesc = mesh.drawCallDescription(vertexLayout, m_scene);
        rtMeshes.push_back({ .firstVertex = drawCallDesc.vertexOffset,
                             .firstIndex = (int32_t)drawCallDesc.firstIndex,
                             .materialIndex = mesh.materialIndex().value_or(0) });
    });

    Buffer& indexBuffer = m_scene.globalIndexBuffer();
    Buffer& vertexBuffer = m_scene.globalVertexBufferForLayout(vertexLayout);

    Buffer& meshBuffer = reg.createBuffer(rtMeshes, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    BindingSet& meshDataBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageRTClosestHit | ShaderStageRTAnyHit), &meshBuffer },
                                                            { 1, ShaderStage(ShaderStageRTClosestHit | ShaderStageRTAnyHit), &indexBuffer },
                                                            { 2, ShaderStage(ShaderStageRTClosestHit | ShaderStageRTAnyHit), &vertexBuffer } });
    meshDataBindingSet.setName("DDGIMeshData");

    return &meshDataBindingSet;
}

Texture* DDGINode::createProbeAtlas(Registry& reg, const std::string& name, const ProbeGrid& probeGrid, const ClearColor& clearColor, Texture::Format format, int probeTileSize, int tileSidePadding) const
{
    ASSERT(probeTileSize > 0);
    ASSERT(tileSidePadding >= 0);

    int sizePerTile = tileSidePadding + probeTileSize + tileSidePadding;

    int numTileSheets = probeGrid.gridDimensions.height();
    Extent2D tileSheetExtents { probeGrid.gridDimensions.width() * sizePerTile,
                                probeGrid.gridDimensions.depth() * sizePerTile };
    Extent2D atlasExtents { tileSheetExtents.width() * numTileSheets, tileSheetExtents.height() };
    
    auto [atlasTexture, reuseMode] = reg.createOrReuseTexture2D(name, atlasExtents, format, Texture::Filters::linear(), Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge());

    if (reuseMode == Registry::ReuseMode::Created) {
        atlasTexture.clear(clearColor);
    }

    return &atlasTexture;
}
