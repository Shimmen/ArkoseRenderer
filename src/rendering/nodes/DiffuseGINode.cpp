#include "DiffuseGINode.h"

#include "CameraState.h"
#include "LightData.h"
#include "ProbeDebug.h"
#include "geometry/Frustum.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>
#include <moos/random.h>
#include <moos/transform.h>

std::string DiffuseGINode::name()
{
    return "diffuse-gi";
}

DiffuseGINode::DiffuseGINode(Scene& scene)
    : RenderGraphNode(DiffuseGINode::name())
    , m_scene(scene)
{
}

static constexpr Extent2D cubemapFaceSize { 64, 64 };
static constexpr Extent2D probeDataDistanceTexSize { 64, 32 };
static constexpr Extent2D probeDataColorSHTexSize { 3, 3 };

static constexpr Texture::Format colorFormat = Texture::Format::RGBA32F;
static constexpr Texture::Format distanceFormat = Texture::Format::RG32F;
static constexpr Texture::Format depthFormat = Texture::Format::Depth32F;

static const auto sphereWrapping = Texture::WrapModes(Texture::WrapMode::ClampToEdge, Texture::WrapMode::ClampToEdge);

void DiffuseGINode::constructNode(Registry& reg)
{
    SCOPED_PROFILE_ZONE();

    m_irradianceProbes = &reg.createTextureArray(m_scene.probeGrid().probeCount(), probeDataColorSHTexSize, colorFormat, Texture::Filters::nearest(), Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge());
    m_irradianceProbes->setName("GIProbeSH");
    reg.publish("irradianceProbes", *m_irradianceProbes);

    m_filteredDistanceProbes = &reg.createTextureArray(m_scene.probeGrid().probeCount(), probeDataDistanceTexSize, distanceFormat, Texture::Filters::linear(), Texture::Mipmap::None, sphereWrapping);
    m_filteredDistanceProbes->setName("GIProbeDistance");
    reg.publish("filteredDistanceProbes", *m_filteredDistanceProbes);
}

RenderGraphNode::ExecuteCallback DiffuseGINode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    // Textures to render to
    Texture& probeColorTex = reg.createTexture2D(cubemapFaceSize, colorFormat);
    Texture& probeDistTex = reg.createCubemapTexture(cubemapFaceSize, distanceFormat);
    Texture& probeDepthTex = reg.createCubemapTexture(cubemapFaceSize, depthFormat);
    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &probeColorTex },
                                                          { RenderTarget::AttachmentType::Color1, &probeDistTex },
                                                          { RenderTarget::AttachmentType::Depth, &probeDepthTex } });

    // Cubemaps to filter from (in theory we could render to the cubemaps directly)
    Texture& probeColorCubemap = reg.createCubemapTexture(cubemapFaceSize, colorFormat);
    Texture& probeDistCubemap = reg.createCubemapTexture(cubemapFaceSize, distanceFormat);

    // Temporary textures for filtering into (in theory we could filter straight into the array textures directly)
    Texture& tempIrradianceProbe = reg.createTexture2D(probeDataColorSHTexSize, colorFormat, Texture::Filters::nearest(), Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge());
    Texture& tempFilteredDistanceProbe = reg.createTexture2D(probeDataDistanceTexSize, distanceFormat, Texture::Filters::linear(), Texture::Mipmap::None, sphereWrapping);

    Buffer& cameraBuffer = reg.createBuffer(6 * sizeof(CameraMatrices), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& cameraBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageVertex | ShaderStageFragment), &cameraBuffer } });

    BindingSet& materialBindingSet = m_scene.globalMaterialBindingSet();
    BindingSet& objectBindingSet = *reg.getBindingSet("scene", "objectSet");
    BindingSet& lightBindingSet = *reg.getBindingSet("scene", "lightSet");

    Shader renderShader = Shader::createBasicRasterize("diffuse-gi/forward.vert", "diffuse-gi/forward.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, renderShader, m_vertexLayout };
    renderStateBuilder.addBindingSet(materialBindingSet);
    renderStateBuilder.addBindingSet(cameraBindingSet);
    renderStateBuilder.addBindingSet(objectBindingSet);
    renderStateBuilder.addBindingSet(lightBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    BindingSet& irradianceFilterBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &tempIrradianceProbe, ShaderBindingType::StorageImage },
                                                                    { 1, ShaderStageCompute, &probeColorCubemap, ShaderBindingType::TextureSampler },
                                                                    { 2, ShaderStageCompute, reg.getTexture("scene", "environmentMap").value(), ShaderBindingType::TextureSampler } });
    Shader irradianceFilterShader = Shader::createCompute("diffuse-gi/filterIrradiance.comp");
    ComputeState& irradianceFilterState = reg.createComputeState(irradianceFilterShader, { &irradianceFilterBindingSet });

    BindingSet& distanceFilterBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &tempFilteredDistanceProbe, ShaderBindingType::StorageImage },
                                                                  { 1, ShaderStageCompute, &probeDistCubemap, ShaderBindingType::TextureSampler } });
    Shader distanceFilterShader = Shader::createCompute("diffuse-gi/filterDistances.comp");
    ComputeState& distanceFilterState = reg.createComputeState(distanceFilterShader, { &distanceFilterBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        float ambientLx = m_scene.ambient();
        static bool useSceneAmbient = false;
        ImGui::Checkbox("Use scene ambient light", &useSceneAmbient);
        if (!useSceneAmbient) {
            static float injectedAmbientLx = 350.0f;
            ImGui::SliderFloat("Injected ambient (lx)", &injectedAmbientLx, 0.0f, 1000.0f, "%.1f");
            ambientLx = injectedAmbientLx;
        }

        uint32_t probeToRender = getProbeIndexForNextToRender();
        moos::ivec3 probeIndex = m_scene.probeGrid().probeIndexFromLinear(probeToRender);
        vec3 probePosition = m_scene.probeGrid().probePositionForIndex(probeIndex);

        // Set up camera matrices for rendering all sides
        // NOTE: Can be compacted, if needed
        std::array<CameraMatrices, 6> sideMatrices;
        std::array<geometry::Frustum, 6> sideFrustums;
        {
            SCOPED_PROFILE_ZONE_NAMED("Setting up camera matrices");

            mat4 projectionFromView = moos::perspectiveProjectionToVulkanClipSpace(moos::HALF_PI, 1.0f, 0.01f, 10.0f);
            mat4 viewFromProjection = inverse(projectionFromView);

            forEachCubemapSide([&](CubemapSide side, uint32_t idx) {
                constexpr vec3 lookDirection[] = {
                    { +1.0, 0.0, 0.0 },
                    { -1.0, 0.0, 0.0 },
                    { 0.0, -1.0, 0.0 },
                    { 0.0, +1.0, 0.0 },
                    { 0.0, 0.0, +1.0 },
                    { 0.0, 0.0, -1.0 }
                };

                constexpr vec3 upDirection[] = {
                    { 0.0, -1.0, 0.0 },
                    { 0.0, -1.0, 0.0 },
                    { 0.0, 0.0, -1.0 },
                    { 0.0, 0.0, +1.0 },
                    { 0.0, -1.0, 0.0 },
                    { 0.0, -1.0, 0.0 }
                };

                vec3 target = probePosition + lookDirection[idx];
                mat4 viewFromWorld = lookAt(probePosition, target, upDirection[idx]);

                sideMatrices[idx] = {
                    .projectionFromView = projectionFromView,
                    .viewFromProjection = viewFromProjection,
                    .viewFromWorld = viewFromWorld,
                    .worldFromView = inverse(viewFromWorld)
                };

                sideFrustums[idx] = geometry::Frustum::createFromProjectionMatrix(projectionFromView * viewFromWorld);
            });

            cameraBuffer.updateData(sideMatrices.data(), sideMatrices.size() * sizeof(CameraMatrices));
        }

        m_scene.forEachMesh([&](size_t, Mesh& mesh) {
            mesh.ensureDrawCall(m_vertexLayout, m_scene);
        });

        forEachCubemapSide([&](CubemapSide side, uint32_t sideIndex) {
            SCOPED_PROFILE_ZONE_NAMED("Drawing cube side");

            // Render this side of the cube
            // NOTE: If we in the future do this recursively (to get N bounces) we don't have to do fancy lighting for this pass,
            //  making it potentially a bit faster. All we have to render is the 0th bounce (everything is black, except light emitters
            //  such as light sources, including the environment map. Directional lights are potentially a bit tricky, though..)
            {
                float clearAlpha = 0.0f; // (important for drawing sky view in filtering stage)
                cmdList.beginRendering(renderState, ClearColor(0, 0, 0, clearAlpha), 1);

                cmdList.bindSet(cameraBindingSet, 0);
                cmdList.bindSet(materialBindingSet, 1);
                cmdList.bindSet(lightBindingSet, 2);
                cmdList.bindSet(objectBindingSet, 3);

                cmdList.pushConstant(ShaderStage(ShaderStageVertex | ShaderStageFragment), sideIndex, 0);
                cmdList.pushConstant(ShaderStage(ShaderStageVertex | ShaderStageFragment), ambientLx, 4);

                cmdList.bindVertexBuffer(m_scene.globalVertexBufferForLayout(m_vertexLayout));
                cmdList.bindIndexBuffer(m_scene.globalIndexBuffer(), m_scene.globalIndexBufferType());

                m_scene.forEachMesh([&](size_t meshIndex, Mesh& mesh) {
                    geometry::Sphere sphere = mesh.boundingSphere().transformed(mesh.transform().worldMatrix());
                    if (!sideFrustums[sideIndex].includesSphere(sphere))
                        return;

                    DrawCall drawCall = mesh.getDrawCall(m_vertexLayout, m_scene);
                    drawCall.firstInstance = meshIndex; // TODO: Put this in some buffer instead!

                    cmdList.issueDrawCall(drawCall);
                });

                cmdList.endRendering();
            }

            // Copy color & distance+distance2 textures to the cubemaps
            {
                cmdList.copyTexture(probeColorTex, probeColorCubemap, 0, sideIndex);
                cmdList.copyTexture(probeDistTex, probeDistCubemap, 0, sideIndex);
            }
        });

        // Prefilter irradiance and encode as SH
        cmdList.setComputeState(irradianceFilterState);
        cmdList.bindSet(irradianceFilterBindingSet, 0);
        cmdList.pushConstant(ShaderStageCompute, m_scene.environmentMultiplier());
        cmdList.pushConstant(ShaderStageCompute, appState.frameIndex(), 4);
        cmdList.dispatch(probeDataColorSHTexSize, { 1, 1, 1 });

        // Prefilter distances and map to spherical
        static float distanceBlurRadius = 0.1f;
        ImGui::SliderFloat("Distance blur radius", &distanceBlurRadius, 0.01, 1.0);

        cmdList.setComputeState(distanceFilterState);
        cmdList.bindSet(distanceFilterBindingSet, 0);
        cmdList.pushConstant(ShaderStageCompute, distanceBlurRadius, 0);
        cmdList.pushConstant(ShaderStageCompute, appState.frameIndex(), 4);
        cmdList.dispatch(probeDataDistanceTexSize, { 16, 16, 1 });

        // Copy color & distance+distance2 textures to the probe data arrays
        // TODO: Later, if we put this in another queue, we have to be very careful here,
        //  because this needs to be done in sync with the main queue while the rest lives on the async compute queue.
        {
            cmdList.copyTexture(tempIrradianceProbe, *m_irradianceProbes, 0, probeToRender);
            cmdList.copyTexture(tempFilteredDistanceProbe, *m_filteredDistanceProbes, 0, probeToRender);
        }
    };
}

uint32_t DiffuseGINode::getProbeIndexForNextToRender() const
{
    // Render the probes in a random order, but make sure that all N probes are rendered once
    // before any node is rendered a second time (like the random tetrominos in tetris)

    static uint32_t s_orderedProbeIndex = 0;
    static std::vector<uint32_t> s_shuffledProbeIndices {};

    uint32_t orderedIndex = s_orderedProbeIndex++;
    uint32_t probeCount = m_scene.probeGrid().probeCount();
    s_orderedProbeIndex %= probeCount;

    if (orderedIndex == 0) {
        // Fill vector if empty
        if (s_shuffledProbeIndices.empty()) {
            for (uint32_t i = 0; i < probeCount; ++i)
                s_shuffledProbeIndices.push_back(i);
        }

        // Shuffle the array
        moos::Random random {};
        for (uint32_t i = 0; i < probeCount - 1; ++i) {
            int otherIndex = random.randomIntInRange(i + 1, probeCount - 1);
            std::swap(s_shuffledProbeIndices[i], s_shuffledProbeIndices[otherIndex]);
        }
    }

    uint32_t probeIndex = s_shuffledProbeIndices[orderedIndex];
    return probeIndex;
}
