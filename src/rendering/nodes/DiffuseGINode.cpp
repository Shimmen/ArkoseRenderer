#include "DiffuseGINode.h"

#include "CameraState.h"
#include "ForwardData.h"
#include "LightData.h"
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

void DiffuseGINode::constructNode(Registry& reg)
{
    m_drawables.clear();
    m_materials.clear();
    m_textures.clear();

    m_scene.forEachMesh([&](size_t, Mesh& mesh) {
        mesh.ensureVertexBuffer(semanticVertexLayout);
        mesh.ensureIndexBuffer();

        // TODO: Remove redundant textures & materials with fancy indexing!
        //  We can't even load Sponza right now without setting up >400 texture
        //  which is obviously stupid. Texture reuse is most critical (more than
        //  material reuse, I think). Texture reuse would have to be done by the
        //  material class, as we ask the material for the texture object.

        Material& material = mesh.material();

        auto pushTexture = [&](Texture* texture) -> size_t {
            size_t textureIndex = m_textures.size();
            m_textures.push_back(texture);
            return textureIndex;
        };

        ForwardMaterial forwardMaterial {};
        forwardMaterial.baseColor = pushTexture(material.baseColorTexture());
        forwardMaterial.emissive = pushTexture(material.emissiveTexture());
        forwardMaterial.metallicRoughness = pushTexture(material.metallicRoughnessTexture());

        int materialIndex = (int)m_materials.size();
        m_materials.push_back(forwardMaterial);

        m_drawables.push_back({ .mesh = mesh,
                                .materialIndex = materialIndex });
    });

    if (m_drawables.size() > FORWARD_MAX_DRAWABLES) {
        LogErrorAndExit("DiffuseGINode: we need to up the number of max drawables that can be handled in the forward pass! We have %u, the capacity is %u.\n",
                        m_drawables.size(), FORWARD_MAX_DRAWABLES);
    }

    if (m_textures.size() > FORWARD_MAX_TEXTURES) {
        LogErrorAndExit("DiffuseGINode: we need to up the number of max textures that can be handled in the forward pass! We have %u, the capacity is %u.\n",
                        m_textures.size(), FORWARD_MAX_TEXTURES);
    }
}

RenderGraphNode::ExecuteCallback DiffuseGINode::constructFrame(Registry& reg) const
{
    constexpr Extent2D cubemapFaceSize { 256, 256 };
    constexpr Extent2D probeDataTexSize { 128, 128 };
    constexpr Texture::Format colorFormat = Texture::Format::RGBA16F;
    constexpr Texture::Format distanceFormat = Texture::Format::RG16F;
    constexpr Texture::Format depthFormat = Texture::Format::Depth32F;

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
    // NOTE: We also have to do stuff like filtering on the depth beforehand..
    //Texture& irradianceProbes = reg.createTextureArray(probeCount(), probeDataTexSize, colorFormat);
    //Texture& filteredDistanceProbes = reg.createTextureArray(probeCount(), probeDataTexSize, distanceFormat);

    // The main render pass, for rendering to the probe textures

    Buffer& cameraBuffer = reg.createBuffer(6 * sizeof(CameraMatrices), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& cameraBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageVertex | ShaderStageFragment), &cameraBuffer } });

    size_t perObjectBufferSize = m_drawables.size() * sizeof(PerForwardObject);
    Buffer& perObjectBuffer = reg.createBuffer(perObjectBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

    size_t materialBufferSize = m_materials.size() * sizeof(ForwardMaterial);
    Buffer& materialBuffer = reg.createBuffer(materialBufferSize, Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    materialBuffer.updateData(m_materials.data(), materialBufferSize);

    BindingSet& objectBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &perObjectBuffer },
                                                          { 1, ShaderStageFragment, &materialBuffer },
                                                          { 2, ShaderStageFragment, m_textures, FORWARD_MAX_TEXTURES } });

    // TODO: Support any (reasonable) number of shadow maps & lights!
    Buffer& lightDataBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& lightBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, &m_scene.sun().shadowMap(), ShaderBindingType::TextureSampler },
                                                         { 1, ShaderStageFragment, &lightDataBuffer } });

    Shader renderShader = Shader::createBasicRasterize("diffuse-gi/forward.vert", "diffuse-gi/forward.frag");
    RenderStateBuilder renderStateBuilder { renderTarget, renderShader, vertexLayout };
    renderStateBuilder.addBindingSet(cameraBindingSet);
    renderStateBuilder.addBindingSet(objectBindingSet);
    renderStateBuilder.addBindingSet(lightBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        static float ambientLx = 0.0f;
        ImGui::SliderFloat("Injected ambient (lx)", &ambientLx, 0.0f, 1000.0f, "%.1f");

        // Update object data
        {
            size_t numDrawables = m_drawables.size();
            std::vector<PerForwardObject> perObjectData { numDrawables };
            for (int i = 0; i < numDrawables; ++i) {
                auto& drawable = m_drawables[i];
                perObjectData[i] = {
                    .worldFromLocal = drawable.mesh.transform().worldMatrix(),
                    .worldFromTangent = mat4(drawable.mesh.transform().worldNormalMatrix()),
                    .materialIndex = drawable.materialIndex
                };
            }
            perObjectBuffer.updateData(perObjectData.data(), numDrawables * sizeof(PerForwardObject));
        }

        // Update light data
        {
            // TODO: Upload all relevant light here, not just the default 'sun' as we do now.
            DirectionalLight& light = m_scene.sun();
            DirectionalLightData dirLightData {
                .colorAndIntensity = { light.color, light.illuminance },
                .worldSpaceDirection = vec4(normalize(light.direction), 0.0),
                .viewSpaceDirection = m_scene.camera().viewMatrix() * vec4(normalize(m_scene.sun().direction), 0.0),
                .lightProjectionFromWorld = light.viewProjection()
            };
            lightDataBuffer.updateData(&dirLightData, sizeof(DirectionalLightData));
        }

        // FIXME: Render them in a random order, but all renders once before any gets a second render (like in tetris!)
        static int s_nextProbeToRender = 0;
        int probeToRender = s_nextProbeToRender;
#if 1
        s_nextProbeToRender += 1;
        if (s_nextProbeToRender >= probeCount()) {
            s_nextProbeToRender = 0;
            LogInfo(" (full GI probe pass completed)\n");
        }
#else
        s_nextProbeToRender = (s_nextProbeToRender + 1) % probeCount();
#endif

        moos::ivec3 probeIndex = probeIndexFromLinear(probeToRender);
        vec3 probePosition = probePositionForIndex(probeIndex);

        // TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME
        // TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME
        // TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME
        probePosition = m_scene.camera().position(); //TODO FIXME TODO FIXME
        // TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME
        // TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME
        // TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME TODO FIXME

        // Set up camera matrices for rendering all sides
        // NOTE: Can be compacted, if needed
        {
            std::array<CameraMatrices, 6> sideMatrices;

            mat4 projectionFromView = moos::perspectiveProjectionToVulkanClipSpace(moos::HALF_PI, 1.0f, 0.1f, 100.0f);
            mat4 viewFromProjection = inverse(projectionFromView);

            forEachCubemapSide([&](CubemapSide side, uint32_t idx) {
                constexpr vec3 lookDirection[] = {
                    { 1.0, 0.0, 0.0 },
                    { -1.0, 0.0, 0.0 },
                    { 0.0, 1.0, 0.0 },
                    { 0.0, -1.0, 0.0 },
                    { 0.0, 0.0, 1.0 },
                    { 0.0, 0.0, -1.0 }
                };

                constexpr vec3 upDirection[] = {
                    { 0.0, -1.0, 0.0 },
                    { 0.0, -1.0, 0.0 },
                    { 0.0, 0.0, 1.0 },
                    { 0.0, 0.0, -1.0 },
                    { 0.0, -1.0, 0.0 },
                    { 0.0, -1.0, 0.0 }
                };

                vec3 target = probePosition + lookDirection[idx];
                mat4 viewFromWorld = moos::lookAt(probePosition, target, upDirection[idx]);

                sideMatrices[idx] = {
                    .projectionFromView = projectionFromView,
                    .viewFromProjection = viewFromProjection,
                    .viewFromWorld = viewFromWorld,
                    .worldFromView = inverse(viewFromWorld)
                };
            });

            cameraBuffer.updateData(sideMatrices.data(), sideMatrices.size() * sizeof(CameraMatrices));
        }

        forEachCubemapSide([&](CubemapSide side, uint32_t idx) {
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

                cmdList.pushConstant(ShaderStage(ShaderStageVertex | ShaderStageFragment), idx, 0);
                cmdList.pushConstant(ShaderStage(ShaderStageVertex | ShaderStageFragment), ambientLx, 4);

                m_scene.forEachMesh([&](size_t meshIndex, Mesh& mesh) {
                    cmdList.drawIndexed(mesh.vertexBuffer(semanticVertexLayout),
                                        mesh.indexBuffer(), mesh.indexCount(), mesh.indexType(),
                                        meshIndex);
                });

                cmdList.endRendering();
            }

            // Copy color & distance+distance2 textures to the cubemaps
            {
                cmdList.copyTexture(probeColorTex, probeColorCubemap, 0, idx);
                cmdList.copyTexture(probeDistTex, probeDistCubemap, 0, idx);
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
