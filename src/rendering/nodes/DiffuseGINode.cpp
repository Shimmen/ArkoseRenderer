#include "DiffuseGINode.h"

#include "CameraState.h"
#include "LightData.h"
#include "ProbeDebug.h"
#include "geometry/Frustum.h"
#include "utility/Logging.h"
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

#if PROBE_DEBUG_HIGH_RES_VIZ
//static constexpr Extent2D cubemapFaceSize { 1024, 1024 };
//static constexpr Extent2D probeDataTexSize { 1024, 512 };
static constexpr Extent2D cubemapFaceSize { 256, 256 };
static constexpr Extent2D probeDataColorTexSize { 256, 128 };
static constexpr Extent2D probeDataDistanceTexSize { 256, 128 };
#else
// TODO: Consider if the distance probes require higher resolution. It seems so, and it makes a bit of sense
//  (consider human luma vs chroma sensitivity and that shadows kind of is luma and irradiance mostly is chroma)
static constexpr Extent2D cubemapFaceSize { 64, 64 };
static constexpr Extent2D probeDataColorTexSize { 32, 16 };
static constexpr Extent2D probeDataDistanceTexSize { 64, 32 };
#endif

static constexpr Texture::Format colorFormat = Texture::Format::RGBA16F;
static constexpr Texture::Format distanceFormat = Texture::Format::RG16F;
static constexpr Texture::Format depthFormat = Texture::Format::Depth32F;

static const auto sphereWrapping = Texture::WrapModes(Texture::WrapMode::Repeat, Texture::WrapMode::ClampToEdge);

void DiffuseGINode::constructNode(Registry& reg)
{
    m_irradianceProbes = &reg.createTextureArray(m_scene.probeGrid().probeCount(), probeDataColorTexSize, colorFormat, Texture::Filters::linear(), Texture::Mipmap::None, sphereWrapping);
    m_filteredDistanceProbes = &reg.createTextureArray(m_scene.probeGrid().probeCount(), probeDataDistanceTexSize, distanceFormat, Texture::Filters::linear(), Texture::Mipmap::None, sphereWrapping);

    reg.publish("irradianceProbes", *m_irradianceProbes);
    reg.publish("filteredDistanceProbes", *m_filteredDistanceProbes);
}

RenderGraphNode::ExecuteCallback DiffuseGINode::constructFrame(Registry& reg) const
{
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

    // Texture arrays for storing final probe data
    Texture& tempIrradianceProbe = reg.createTexture2D(probeDataColorTexSize, colorFormat, Texture::Filters::linear(), Texture::Mipmap::None, sphereWrapping); // FIXME: Use a texture array!
    Texture& tempFilteredDistanceProbe = reg.createTexture2D(probeDataDistanceTexSize, distanceFormat, Texture::Filters::linear(), Texture::Mipmap::None, sphereWrapping); // FIXME: Use a texture array!

    // The main render pass, for rendering to the probe textures

    Buffer& cameraBuffer = reg.createBuffer(6 * sizeof(CameraMatrices), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& cameraBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageVertex | ShaderStageFragment), &cameraBuffer } });

    BindingSet& objectBindingSet = *reg.getBindingSet("scene", "objectSet");
    BindingSet& lightBindingSet = *reg.getBindingSet("scene", "lightSet");

    Shader renderShader = Shader::createBasicRasterize("diffuse-gi/forward.vert", "diffuse-gi/forward.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, renderShader, vertexLayout };
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

    m_scene.forEachMesh([&](size_t, Mesh& mesh) {
        mesh.ensureVertexBuffer(semanticVertexLayout);
        mesh.ensureIndexBuffer();
    });

    return [&](const AppState& appState, CommandList& cmdList) {
        float ambientLx = m_scene.ambient();
        static bool useSceneAmbient = true;
        ImGui::Checkbox("Use scene ambient light", &useSceneAmbient);
        if (!useSceneAmbient) {
            static float injectedAmbientLx = 0.0f;
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

        forEachCubemapSide([&](CubemapSide side, uint32_t sideIndex) {
            // Render this side of the cube
            // NOTE: If we in the future do this recursively (to get N bounces) we don't have to do fancy lighting for this pass,
            //  making it potentially a bit faster. All we have to render is the 0th bounce (everything is black, except light emitters
            //  such as light sources, including the environment map. Directional lights are potentially a bit tricky, though..)
            {
                float clearAlpha = 0.0f; // (important for drawing sky view in filtering stage)
                cmdList.beginRendering(renderState, ClearColor(0, 0, 0, clearAlpha), 1);

                cmdList.bindSet(cameraBindingSet, 0);
                cmdList.bindSet(objectBindingSet, 1);
                cmdList.bindSet(lightBindingSet, 2);

                cmdList.pushConstant(ShaderStage(ShaderStageVertex | ShaderStageFragment), sideIndex, 0);
                cmdList.pushConstant(ShaderStage(ShaderStageVertex | ShaderStageFragment), ambientLx, 4);

                m_scene.forEachMesh([&](size_t meshIndex, Mesh& mesh) {
                    geometry::Sphere sphere = mesh.boundingSphere().transformed(mesh.transform().worldMatrix());
                    if (!sideFrustums[sideIndex].includesSphere(sphere))
                        return;

                    cmdList.drawIndexed(mesh.vertexBuffer(semanticVertexLayout),
                                        mesh.indexBuffer(), mesh.indexCount(), mesh.indexType(),
                                        meshIndex);
                });

                cmdList.endRendering();
            }

            // Copy color & distance+distance2 textures to the cubemaps
            {
                cmdList.copyTexture(probeColorTex, probeColorCubemap, 0, sideIndex);
                cmdList.copyTexture(probeDistTex, probeDistCubemap, 0, sideIndex);
            }
        });

        // Prefilter irradiance and map to spherical
        cmdList.setComputeState(irradianceFilterState);
        cmdList.bindSet(irradianceFilterBindingSet, 0);
        cmdList.pushConstant(ShaderStageCompute, m_scene.environmentMultiplier());
        cmdList.pushConstant(ShaderStageCompute, appState.frameIndex(), 4);
        cmdList.dispatch(probeDataColorTexSize, { 16, 16, 1 });

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
        LogInfo("Reset!\n");

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
