#include "GpuScene.h"

#include "backend/Resources.h"
#include "rendering/Registry.h"
#include "utility/Logging.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <moos/aabb.h>
#include <moos/transform.h>

// Shared shader headers
using uint = uint32_t;
#include "RTData.h"
#include "LightData.h"
#include "CameraState.h"

GpuScene::GpuScene(Scene& scene, Extent2D initialMainViewportSize)
    : m_scene(scene)
{
}

RenderPipelineNode::ExecuteCallback GpuScene::construct(GpuScene&, Registry& reg)
{
    // G-Buffer textures
    {
        Extent2D windowExtent = reg.windowRenderTarget().extent();

        Texture& depthTexture = reg.createTexture2D(windowExtent, Texture::Format::Depth24Stencil8, Texture::Filters::nearest());
        reg.publish("SceneDepth", depthTexture);

        // rgb: scene color, a: unused
        Texture& colorTexture = reg.createTexture2D(windowExtent, Texture::Format::RGBA16F);
        reg.publish("SceneColor", colorTexture);

        // rg: encoded normal, ba: velocity in image plane (2D)
        Texture& normalVelocityTexture = reg.createTexture2D(windowExtent, Texture::Format::RGBA16F);
        reg.publish("SceneNormalVelocity", normalVelocityTexture);

        // r: roughness, g: metallic, b: unused, a: unused
        Texture& materialTexture = reg.createTexture2D(windowExtent, Texture::Format::RGBA16F);
        reg.publish("SceneMaterial", materialTexture);

        // rgb: base color, a: unused
        Texture& baseColorTexture = reg.createTexture2D(windowExtent, Texture::Format::RGBA8);
        reg.publish("SceneBaseColor", baseColorTexture);

        // rgb: diffuse color, a: unused
        Texture& diffueGiTexture = reg.createTexture2D(windowExtent, Texture::Format::RGBA16F);
        reg.publish("DiffuseGI", diffueGiTexture);
    }

    Buffer& cameraBuffer = reg.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::GpuOnly);
    BindingSet& cameraBindingSet = reg.createBindingSet({ { 0, ShaderStage::AnyRasterize, &cameraBuffer } });
    reg.publish("SceneCameraData", cameraBuffer);
    reg.publish("SceneCameraSet", cameraBindingSet);

    // Object data stuff
    // TODO: Resize the buffer if needed when more meshes are added
    size_t objectDataBufferSize = scene().meshCount() * sizeof(ShaderDrawable);
    Buffer& objectDataBuffer = reg.createBuffer(objectDataBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    objectDataBuffer.setName("SceneObjectData");
    BindingSet& objectBindingSet = reg.createBindingSet({ { 0, ShaderStage::Vertex, &objectDataBuffer } });
    reg.publish("objectSet", objectBindingSet);

    if (doesMaintainRayTracingScene()) {

        // TODO: Resize the buffer if needed when more meshes are added

        std::vector<RTTriangleMesh> rtMeshes {};
        scene().forEachMesh([&](size_t meshIdx, Mesh& mesh) {
            const DrawCallDescription& drawCallDesc = mesh.drawCallDescription(m_rayTracingVertexLayout, *this);
            rtMeshes.push_back({ .firstVertex = drawCallDesc.vertexOffset,
                                 .firstIndex = (int32_t)drawCallDesc.firstIndex,
                                 .materialIndex = mesh.materialIndex().value_or(0) });
        });

        Buffer& meshBuffer = reg.createBuffer(rtMeshes, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
        BindingSet& rtMeshDataBindingSet = reg.createBindingSet({ { 0, ShaderStage::AnyRayTrace, &meshBuffer },
                                                                  { 1, ShaderStage::AnyRayTrace, &globalIndexBuffer() },
                                                                  { 2, ShaderStage::AnyRayTrace, &globalVertexBufferForLayout(m_rayTracingVertexLayout) } });

        reg.publish("SceneRTMeshDataSet", rtMeshDataBindingSet);
    }

    // Light shadow data stuff (todo: make not fixed!)
    uint32_t numShadowCastingLights = scene().forEachShadowCastingLight([](size_t, Light&) {});
    Buffer& lightShadowDataBuffer = reg.createBuffer(numShadowCastingLights * sizeof(PerLightShadowData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("SceneShadowData", lightShadowDataBuffer);

    // Light data stuff
    Buffer& lightMetaDataBuffer = reg.createBuffer(sizeof(LightMetaData), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::GpuOnly);
    lightMetaDataBuffer.setName("SceneLightMetaData");
    Buffer& dirLightDataBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    dirLightDataBuffer.setName("SceneDirectionalLightData");
    Buffer& spotLightDataBuffer = reg.createBuffer(10 * sizeof(SpotLightData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    spotLightDataBuffer.setName("SceneSpotLightData");
    std::vector<Texture*> iesProfileLUTs;
    std::vector<Texture*> shadowMaps;
    // TODO: We need to be able to update the shadow map binding. Right now we can only do it once, at creation.
    scene().forEachLight([&](size_t, Light& light) {
        if (light.type() == Light::Type::SpotLight)
            iesProfileLUTs.push_back(&((SpotLight&)light).iesProfileLookupTexture()); // all this light stuff needs cleanup...
        if (light.castsShadows())
            shadowMaps.push_back(&light.shadowMap());
    });
    // We can't upload empty texture arrays (for now.. would be better to fix deeper int the stack)
    if (iesProfileLUTs.empty())
        iesProfileLUTs.push_back(&reg.createPixelTexture(vec4(1.0f), true));

    BindingSet& lightBindingSet = reg.createBindingSet({ { 0, ShaderStage::Any, &lightMetaDataBuffer },
                                                         { 1, ShaderStage::Any, &dirLightDataBuffer },
                                                         { 2, ShaderStage::Any, &spotLightDataBuffer },
                                                         { 3, ShaderStage::Any, shadowMaps },
                                                         { 4, ShaderStage::Any, iesProfileLUTs } });
    reg.publish("SceneLightSet", lightBindingSet);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (m_sceneDataNeedsRebuild) {
            // We shouldn't need to rebuild the whole thing, just append and potentially remove some stuff.. But the
            // distinction here is that it wouldn't be enough to just update some matrices, e.g. if an object was moved.
            // If we save the vector of textures & materials we can probably resuse a lot of calculations. There is no
            // rush with that though, as currently we can't even make changes that would require a rebuild..
            // TODO: There is no reason this is a separate path from the normal GpuScene render pipeline execute function!
            rebuildGpuSceneData();
            m_sceneDataNeedsRebuild = false;
        }

        // Update camera data
        {
            const Camera& camera = this->camera();

            mat4 pixelFromView = camera.pixelProjectionMatrix();
            mat4 projectionFromView = camera.projectionMatrix();
            mat4 viewFromWorld = camera.viewMatrix();

            CameraState cameraState {
                .projectionFromView = projectionFromView,
                .viewFromProjection = inverse(projectionFromView),
                .viewFromWorld = viewFromWorld,
                .worldFromView = inverse(viewFromWorld),

                .previousFrameProjectionFromView = camera.previousFrameProjectionMatrix(),
                .previousFrameViewFromWorld = camera.previousFrameViewMatrix(),

                .pixelFromView = pixelFromView,
                .viewFromPixel = inverse(pixelFromView),

                .near = camera.zNear,
                .far = camera.zFar,

                .iso = camera.iso,
                .aperture = camera.aperture,
                .shutterSpeed = camera.shutterSpeed,
                .exposureCompensation = camera.exposureCompensation,
            };

            uploadBuffer.upload(cameraState, cameraBuffer);
        }

        // Update object data
        {
            std::vector<ShaderDrawable> objectData {};
            scene().forEachMesh([&](size_t, Mesh& mesh) {
                objectData.push_back(ShaderDrawable { .worldFromLocal = mesh.transform().worldMatrix(),
                                                      .worldFromTangent = mat4(mesh.transform().worldNormalMatrix()),
                                                      .previousFrameWorldFromLocal = mesh.transform().previousFrameWorldMatrix(),
                                                      .materialIndex = mesh.materialIndex().value_or(0) });
            });

            uploadBuffer.upload(objectData, objectDataBuffer);
        }

        // Update exposure data
        {
            if (camera().useAutomaticExposure) {
                ASSERT_NOT_REACHED();
            } else {
                // See camera.glsl for reference
                auto& camera = this->camera();
                float ev100 = std::log2f((camera.aperture * camera.aperture) / camera.shutterSpeed * 100.0f / camera.iso);
                float maxLuminance = 1.2f * std::pow(2.0f, ev100);
                m_lightPreExposure = 1.0f / maxLuminance;
            }
        }

        // Update light data
        {
            mat4 viewFromWorld = camera().viewMatrix();
            mat4 worldFromView = inverse(viewFromWorld);

            int nextShadowMapIndex = 0;
            std::vector<DirectionalLightData> dirLightData;
            std::vector<SpotLightData> spotLightData;

            scene().forEachLight([&](size_t, Light& light) {
                int shadowMapIndex = light.castsShadows() ? nextShadowMapIndex++ : -1;
                ShadowMapData shadowMapData { .textureIndex = shadowMapIndex };

                vec3 lightColor = light.color * light.intensityValue() * lightPreExposure();

                switch (light.type()) {
                case Light::Type::DirectionalLight: {
                    dirLightData.emplace_back(DirectionalLightData { .shadowMap = shadowMapData,
                                                                     .color = lightColor,
                                                                     .exposure = lightPreExposure(),
                                                                     .worldSpaceDirection = vec4(normalize(light.forwardDirection()), 0.0),
                                                                     .viewSpaceDirection = viewFromWorld * vec4(normalize(light.forwardDirection()), 0.0),
                                                                     .lightProjectionFromWorld = light.viewProjection(),
                                                                     .lightProjectionFromView = light.viewProjection() * worldFromView });
                    break;
                }
                case Light::Type::SpotLight: {
                    SpotLight& spotLight = static_cast<SpotLight&>(light);
                    spotLightData.emplace_back(SpotLightData { .shadowMap = shadowMapData,
                                                               .color = lightColor,
                                                               .exposure = lightPreExposure(),
                                                               .worldSpaceDirection = vec4(normalize(light.forwardDirection()), 0.0f),
                                                               .viewSpaceDirection = viewFromWorld * vec4(normalize(light.forwardDirection()), 0.0f),
                                                               .lightProjectionFromWorld = light.viewProjection(),
                                                               .lightProjectionFromView = light.viewProjection() * worldFromView,
                                                               .worldSpacePosition = vec4(light.position(), 0.0f),
                                                               .viewSpacePosition = viewFromWorld * vec4(light.position(), 1.0f),
                                                               .outerConeHalfAngle = spotLight.outerConeAngle / 2.0f,
                                                               .iesProfileIndex = 0 /* todo: set correctly */,
                                                               ._pad0 = vec2() });
                    break;
                }
                case Light::Type::PointLight:
                default:
                    ASSERT_NOT_REACHED();
                    break;
                }
            });

            uploadBuffer.upload(dirLightData, dirLightDataBuffer);
            uploadBuffer.upload(spotLightData, spotLightDataBuffer);

            LightMetaData metaData { .numDirectionalLights = (int)dirLightData.size(),
                                     .numSpotLights = (int)spotLightData.size() };
            uploadBuffer.upload(metaData, lightMetaDataBuffer);

            std::vector<PerLightShadowData> shadowData;
            scene().forEachShadowCastingLight([&](size_t, Light& light) {
                shadowData.push_back({ .lightViewFromWorld = light.lightViewMatrix(),
                                       .lightProjectionFromWorld = light.viewProjection(),
                                       .constantBias = light.constantBias(),
                                       .slopeBias = light.slopeBias() });
            });
            uploadBuffer.upload(shadowData, lightShadowDataBuffer);
        }

        cmdList.executeBufferCopyOperations(uploadBuffer);

        if (doesMaintainRayTracingScene()) {

            TopLevelAS& sceneTlas = globalTopLevelAccelerationStructure();

            sceneTlas.updateInstanceDataWithUploadBuffer(m_rayTracingGeometryInstances, uploadBuffer);
            cmdList.executeBufferCopyOperations(uploadBuffer);

            cmdList.rebuildTopLevelAcceratationStructure(sceneTlas);

        }
    };
}

void GpuScene::updateEnvironmentMap(Scene::EnvironmentMap& environmentMap)
{
    Backend& backend = Backend::get();
    m_environmentMapTexture = environmentMap.assetPath.empty()
        ? Texture::createFromPixel(backend, vec4(1.0f), true)
        : Texture::createFromImagePath(backend, environmentMap.assetPath, true, false, Texture::WrapModes::repeatAll());
}

Texture& GpuScene::environmentMapTexture()
{
    ASSERT(m_environmentMapTexture);
    return *m_environmentMapTexture;
}

void GpuScene::registerModel(Model& model)
{
    m_models.push_back(&model);
    m_sceneDataNeedsRebuild = true;
}

void GpuScene::registerLight(SpotLight& light)
{
    m_spotLights.push_back(&light);
    m_sceneDataNeedsRebuild = true;
}

void GpuScene::registerLight(DirectionalLight& light)
{
    m_directionalLights.push_back(&light);
    m_sceneDataNeedsRebuild = true;
}

void GpuScene::setShouldMaintainRayTracingScene(Badge<Scene>, bool value)
{
    m_maintainRayTracingScene = value;
    m_sceneDataNeedsRebuild = true;
}

DrawCallDescription GpuScene::fitVertexAndIndexDataForMesh(Badge<Mesh>, const Mesh& mesh, const VertexLayout& layout, std::optional<DrawCallDescription> alignWith)
{
    const size_t initialIndexBufferSize = 100'000 * sizeof(uint32_t);
    const size_t initialVertexBufferSize = 50'000 * layout.packedVertexSize();

    bool doAlign = alignWith.has_value();
    ASSERT(!alignWith || alignWith->sourceMesh == &mesh);

    std::vector<uint8_t> vertexData = mesh.vertexData(layout);

    auto entry = m_globalVertexBuffers.find(layout);
    if (entry == m_globalVertexBuffers.end()) {

        size_t offset = doAlign ? (alignWith->vertexOffset * layout.packedVertexSize()) : 0;
        size_t minRequiredBufferSize = offset + vertexData.size();

        m_globalVertexBuffers[layout] = Backend::get().createBuffer(std::max(initialVertexBufferSize, minRequiredBufferSize), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        m_globalVertexBuffers[layout]->setName("SceneVertexBuffer");
    }

    Buffer& vertexBuffer = *m_globalVertexBuffers[layout];
    size_t newDataStartOffset = doAlign
        ? alignWith->vertexOffset * layout.packedVertexSize()
        : m_nextFreeVertexIndex * layout.packedVertexSize();

    vertexBuffer.updateDataAndGrowIfRequired(vertexData.data(), vertexData.size(), newDataStartOffset);

    if (doAlign) {
        // TODO: Maybe ensure we haven't already fitted this mesh+layout combo and is just overwriting at this point. Well, before doing it I guess..
        DrawCallDescription reusedDrawCall = alignWith.value();
        reusedDrawCall.vertexBuffer = m_globalVertexBuffers[layout].get();
        return reusedDrawCall;
    }

    uint32_t vertexCount = (uint32_t)mesh.vertexCountForLayout(layout);
    uint32_t vertexOffset = m_nextFreeVertexIndex;
    m_nextFreeVertexIndex += vertexCount;


    DrawCallDescription drawCall {};
    drawCall.sourceMesh = &mesh;

    drawCall.vertexBuffer = &vertexBuffer;
    drawCall.vertexCount = vertexCount;
    drawCall.vertexOffset = vertexOffset;

    // Fit index data
    {
        std::vector<uint32_t> indexData = mesh.indexData();
        size_t requiredAdditionalSize = indexData.size() * sizeof(uint32_t);

        if (m_global32BitIndexBuffer == nullptr) {
            m_global32BitIndexBuffer = Backend::get().createBuffer(std::max(initialIndexBufferSize, requiredAdditionalSize), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
            m_global32BitIndexBuffer->setName("SceneIndexBuffer");
        }

        uint32_t firstIndex = m_nextFreeIndex;
        m_nextFreeIndex += (uint32_t)indexData.size();

        m_global32BitIndexBuffer->updateDataAndGrowIfRequired(indexData.data(), requiredAdditionalSize, firstIndex * sizeof(uint32_t));

        drawCall.indexBuffer = m_global32BitIndexBuffer.get();
        drawCall.indexCount = (uint32_t)indexData.size();
        drawCall.indexType = IndexType::UInt32;
        drawCall.firstIndex = firstIndex;
    }

    return drawCall;
}

Buffer& GpuScene::globalVertexBufferForLayout(const VertexLayout& layout) const
{
    auto entry = m_globalVertexBuffers.find(layout);
    if (entry == m_globalVertexBuffers.end())
        LogErrorAndExit("Can't get vertex buffer for layout since it has not been created! Please ensureDrawCallIsAvailable for at least one mesh before calling this.\n");
    return *entry->second;
}

Buffer& GpuScene::globalIndexBuffer() const
{
    if (m_global32BitIndexBuffer == nullptr)
        LogErrorAndExit("Can't get global index buffer since it has not been created! Please ensureDrawCallIsAvailable for at least one indexed mesh before calling this.\n");
    return *m_global32BitIndexBuffer;
}

IndexType GpuScene::globalIndexBufferType() const
{
    // For simplicity we keep a single 32-bit index buffer, since every mesh should fit in there.
    return IndexType::UInt32;
}

constexpr bool operator==(const ShaderMaterial& lhs, const ShaderMaterial& rhs)
{
    if (lhs.baseColor != rhs.baseColor)
        return false;
    if (lhs.normalMap != rhs.normalMap)
        return false;
    if (lhs.metallicRoughness != rhs.metallicRoughness)
        return false;
    if (lhs.emissive != rhs.emissive)
        return false;
    return true;
}

void GpuScene::rebuildGpuSceneData()
{
    SCOPED_PROFILE_ZONE();

    m_usedTextures.clear();
    m_usedMaterials.clear();
    m_rayTracingGeometryInstances.clear();

    std::unordered_map<Texture*, int> textureIndices;
    auto pushTexture = [&](Texture* texture) -> int {
        auto entry = textureIndices.find(texture);
        if (entry != textureIndices.end())
            return entry->second;

        int textureIndex = static_cast<int>(m_usedTextures.size());
        textureIndices[texture] = textureIndex;
        m_usedTextures.push_back(texture);

        return textureIndex;
    };

    auto pushMaterial = [&](ShaderMaterial shaderMaterial) -> int {
        // Would be nice if we could hash them..
        for (int idx = 0; idx < m_usedMaterials.size(); ++idx) {
            if (m_usedMaterials[idx] == shaderMaterial)
                return idx;
        }

        int materialIndex = static_cast<int>(m_usedMaterials.size());
        m_usedMaterials.push_back(shaderMaterial);

        return materialIndex;
    };

    int numMeshes = scene().forEachMesh([&](size_t meshIdx, Mesh& mesh) {

        Material& material = mesh.material();
        int materialIndex = pushMaterial(ShaderMaterial {
            .baseColor = pushTexture(material.baseColorTexture()),
            .normalMap = pushTexture(material.normalMapTexture()),
            .metallicRoughness = pushTexture(material.metallicRoughnessTexture()),
            .emissive = pushTexture(material.emissiveTexture()),
            .blendMode = material.blendModeValue(),
            .maskCutoff = material.maskCutoff,
        });

        ASSERT(materialIndex >= 0);
        mesh.setMaterialIndex({}, materialIndex);

        if (doesMaintainRayTracingScene()) {

            VertexLayout vertexLayout = { VertexComponent::Position3F };
            size_t vertexStride = vertexLayout.packedVertexSize();
            RTVertexFormat vertexFormat = RTVertexFormat::XYZ32F;

            const DrawCallDescription& drawCallDesc = mesh.drawCallDescription(vertexLayout, *this);
            ASSERT(drawCallDesc.type == DrawCallDescription::Type ::Indexed);

            IndexType indexType = globalIndexBufferType();
            size_t indexStride = sizeofIndexType(indexType);

            uint32_t indexOfFirstVertex = drawCallDesc.vertexOffset; // Yeah this is confusing naming for sure.. Offset should probably always be byte offset
            size_t vertexOffset = indexOfFirstVertex * vertexStride;

            RTTriangleGeometry geometry { .vertexBuffer = *drawCallDesc.vertexBuffer,
                                          .vertexCount = drawCallDesc.vertexCount,
                                          .vertexOffset = vertexOffset,
                                          .vertexStride = vertexStride,
                                          .vertexFormat = vertexFormat,
                                          .indexBuffer = *drawCallDesc.indexBuffer,
                                          .indexCount = drawCallDesc.indexCount,
                                          .indexOffset = indexStride * drawCallDesc.firstIndex,
                                          .indexType = indexType,
                                          .transform = mesh.transform().localMatrix() };

            uint8_t hitMask = 0x00;
            switch (material.blendMode) {
            case Material::BlendMode::Opaque:
                hitMask = RT_HIT_MASK_OPAQUE;
                break;
            case Material::BlendMode::Masked:
                hitMask = RT_HIT_MASK_MASKED;
                break;
            case Material::BlendMode::Translucent:
                hitMask = RT_HIT_MASK_BLEND;
                break;
            default:
                ASSERT_NOT_REACHED();
            }
            ASSERT(hitMask != 0);

            m_sceneBottomLevelAccelerationStructures.emplace_back(Backend::get().createBottomLevelAccelerationStructure({ geometry }));
            BottomLevelAS& blas = *m_sceneBottomLevelAccelerationStructures.back();

            // TODO: Probably create a geometry per mesh but only a single instance per model, and use the SBT for material lookup!
            RTGeometryInstance instance = { .blas = blas,
                                            .transform = mesh.model()->transform(),
                                            .shaderBindingTableOffset = 0, // todo: generalize!
                                            .customInstanceId = static_cast<uint32_t>(meshIdx),
                                            .hitMask = hitMask };

            m_rayTracingGeometryInstances.push_back(instance);
        }

    });

    //if (m_usedTextures.size() > SCENE_MAX_TEXTURES) {
    //    LogErrorAndExit("GpuScene: we need to up the number of max textures that can be handled by the scene! We have %u, the capacity is %u.\n",
    //                    m_usedTextures.size(), SCENE_MAX_TEXTURES);
    //}

    // Create material buffer
    size_t materialBufferSize = m_usedMaterials.size() * sizeof(ShaderMaterial);
    m_materialDataBuffer = Backend::get().createBuffer(materialBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_materialDataBuffer->updateData(m_usedMaterials.data(), materialBufferSize);
    m_materialDataBuffer->setName("SceneMaterialData");

    m_materialBindingSet = Backend::get().createBindingSet({ { 0, ShaderStage::Any, m_materialDataBuffer.get() },
                                                             { 1, ShaderStage::Any, m_usedTextures } });
    m_materialBindingSet->setName("SceneMaterialSet");

    if (doesMaintainRayTracingScene() && m_rayTracingGeometryInstances.size() > 0) {
        // TODO: We need to handle the case where we end up with more instances then required. Should that just force a full recreation maybe?
        m_sceneTopLevelAccelerationStructure = Backend::get().createTopLevelAccelerationStructure(InitialMaxRayTracingGeometryInstanceCount, m_rayTracingGeometryInstances);
    }

    // Just rebuilt.
    m_sceneDataNeedsRebuild = false;
}

BindingSet& GpuScene::globalMaterialBindingSet() const
{
    ASSERT(m_materialBindingSet);
    return *m_materialBindingSet;
}

TopLevelAS& GpuScene::globalTopLevelAccelerationStructure() const
{
    ASSERT(doesMaintainRayTracingScene());
    ASSERT(m_sceneTopLevelAccelerationStructure);
    return *m_sceneTopLevelAccelerationStructure;
}

void GpuScene::drawGui()
{
    ImGui::Begin("GPU Scene");

    ImGui::Text("Number of managed resources:");
    ImGui::Columns(3);
    ImGui::Text("meshes: %u", scene().meshCount()); // TODO: replace with something like "drawables"
    ImGui::NextColumn();
    ImGui::Text("materials: %u", m_usedMaterials.size());
    ImGui::NextColumn();
    ImGui::Text("textures: %u", m_usedTextures.size());
    ImGui::Columns(1);

    ImGui::End();
}
