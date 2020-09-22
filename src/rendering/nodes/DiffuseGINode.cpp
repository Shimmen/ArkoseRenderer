#include "DiffuseGINode.h"

#include "CameraState.h"
#include "LightData.h"
#include "geometry/Frustum.h"
#include "utility/Logging.h"
#include <imgui.h>
#include <mooslib/transform.h>

std::string DiffuseGINode::name()
{
    return "diffuse-gi";
}

DiffuseGINode::DiffuseGINode(Scene& scene, ProbeGridDescription gridDescription)
    : RenderGraphNode(DiffuseGINode::name())
    , m_scene(scene)
    , m_grid(gridDescription)
{
}

RenderGraphNode::ExecuteCallback DiffuseGINode::constructFrame(Registry& reg) const
{
#if 0
    static constexpr Extent2D cubemapFaceSize { 256, 256 };
    static constexpr Extent2D probeDataTexSize { 64, 64 };
#else
    static constexpr Extent2D cubemapFaceSize { 1024, 1024 };
    static constexpr Extent2D probeDataTexSize { 1024, 512 };
#endif

    static constexpr Texture::Format colorFormat = Texture::Format::RGBA16F;
    static constexpr Texture::Format distanceFormat = Texture::Format::RG16F;
    static constexpr Texture::Format depthFormat = Texture::Format::Depth32F;

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

    reg.publish("probeColorCubemap", probeColorCubemap);

    // Texture arrays for storing final probe data
    // NOTE: We also have to do stuff like filtering on the depth beforehand..
    Texture& irradianceProbe = reg.createTexture2D(probeDataTexSize, colorFormat, Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge()); // FIXME: Use a texture array!
    //Texture& irradianceProbes = reg.createTextureArray(probeCount(), probeDataTexSize, colorFormat);
    //Texture& filteredDistanceProbes = reg.createTextureArray(probeCount(), probeDataTexSize, distanceFormat);

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

    m_scene.forEachMesh([&](size_t, Mesh& mesh) {
        mesh.ensureVertexBuffer(semanticVertexLayout);
        mesh.ensureIndexBuffer();
    });

    return [&](const AppState& appState, CommandList& cmdList) {
        static float ambientLx = 0.0f;
        ImGui::SliderFloat("Injected ambient (lx)", &ambientLx, 0.0f, 1000.0f, "%.1f");

        // FIXME: Render them in a random order, but all renders once before any gets a second render (like in tetris!)
        static int s_nextProbeToRender = 0;
        int probeToRender = s_nextProbeToRender++;
        if (s_nextProbeToRender >= probeCount()) {
            s_nextProbeToRender = 0;
            //LogInfo(" (full GI probe pass completed)\n");
        }

        moos::ivec3 probeIndex = probeIndexFromLinear(probeToRender);
        vec3 probePosition = probePositionForIndex(probeIndex);

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

        // TODO: Prefilter irradiance and map to octahedral
        // TODO: Chebychev stuff (filter distances) and map to octahedral
    };
}

int DiffuseGINode::probeCount() const
{
    return m_grid.gridDimensions.width()
        * m_grid.gridDimensions.height()
        * m_grid.gridDimensions.depth();
}

moos::ivec3 DiffuseGINode::probeIndexFromLinear(int index) const
{
    int xySize = m_grid.gridDimensions.width() * m_grid.gridDimensions.height();
    int zIndex = index / xySize;
    index %= xySize;

    int ySize = m_grid.gridDimensions.height();
    int yIndex = index / ySize;
    index %= ySize;

    int xIndex = index;

    return { xIndex, yIndex, zIndex };
}

vec3 DiffuseGINode::probePositionForIndex(moos::ivec3 index) const
{
    vec3 floatIndex = { (float)index.x,
                        (float)index.y,
                        (float)index.z };
    return m_grid.offsetToFirst + (floatIndex * m_grid.probeSpacing);
}
