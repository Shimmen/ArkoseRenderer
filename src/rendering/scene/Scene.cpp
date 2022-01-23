#include "Scene.h"

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

Scene::Scene(Registry& registry, Extent2D initialMainViewportSize)
    : m_registry(registry)
    , m_mainViewportSize(initialMainViewportSize)
{
}

void Scene::newFrame(Extent2D mainViewportSize, bool firstFrame)
{
    m_mainViewportSize = mainViewportSize;

    camera().newFrame({}, mainViewportSize, firstFrame);

    // NOTE: We only want to do this on leaf-nodes right now, i.e. meshes not models.
    forEachMesh([&](size_t meshIdx, Mesh& mesh) {
        mesh.transform().newFrame({}, firstFrame);
    });
}

RenderPipelineNode::ExecuteCallback Scene::construct(Scene&, Registry& reg)
{
    Buffer& cameraBuffer = reg.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::GpuOnly);
    BindingSet& cameraBindingSet = reg.createBindingSet({ { 0, ShaderStage::AnyRasterize, &cameraBuffer } });
    reg.publish("SceneCameraData", cameraBuffer);
    reg.publish("SceneCameraSet", cameraBindingSet);

    // Environment mapping stuff
    Texture& envTexture = environmentMap().empty()
        ? reg.createPixelTexture(vec4(1.0f), true)
        : reg.loadTexture2D(environmentMap(), true, false);
    reg.publish("SceneEnvironmentMap", envTexture);

    // Object data stuff
    // TODO: Resize the buffer if needed when more meshes are added
    size_t objectDataBufferSize = meshCount() * sizeof(ShaderDrawable);
    Buffer& objectDataBuffer = reg.createBuffer(objectDataBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    objectDataBuffer.setName("SceneObjectData");
    BindingSet& objectBindingSet = reg.createBindingSet({ { 0, ShaderStage::Vertex, &objectDataBuffer } });
    reg.publish("objectSet", objectBindingSet);

    if (doesMaintainRayTracingScene()) {

        // TODO: Resize the buffer if needed when more meshes are added

        std::vector<RTTriangleMesh> rtMeshes {};
        forEachMesh([&](size_t meshIdx, Mesh& mesh) {
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

    // Light shadow data stuff
    Buffer& lightShadowDataBuffer = reg.createBuffer(SCENE_MAX_SHADOW_MAPS * sizeof(PerLightShadowData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
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
    forEachLight([&](size_t, Light& light) {
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
                                                         { 3, ShaderStage::Any, shadowMaps, SCENE_MAX_SHADOW_MAPS },
                                                         { 4, ShaderStage::Any, iesProfileLUTs, SCENE_MAX_IES_LUT } });
    reg.publish("SceneLightSet", lightBindingSet);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (m_sceneDataNeedsRebuild) {
            // We shouldn't need to rebuild the whole thing, just append and potentially remove some stuff.. But the
            // distinction here is that it wouldn't be enough to just update some matrices, e.g. if an object was moved.
            // If we save the vector of textures & materials we can probably resuse a lot of calculations. There is no
            // rush with that though, as currently we can't even make changes that would require a rebuild..
            // TODO: There is no reason this is a separate path from the normal Scene render pipeline execute function!
            rebuildGpuSceneData();
            m_sceneDataNeedsRebuild = false;
        }

        drawSceneGui();
        drawSceneGizmos();

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
            forEachMesh([&](size_t, Mesh& mesh) {
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

            forEachLight([&](size_t, Light& light) {
                int shadowMapIndex = light.castsShadows() ? nextShadowMapIndex++ : -1;
                ShadowMapData shadowMapData { .textureIndex = shadowMapIndex };

                vec3 lightColor = light.color * light.intensityValue() * lightPreExposureValue();

                switch (light.type()) {
                case Light::Type::DirectionalLight: {
                    dirLightData.emplace_back(DirectionalLightData { .shadowMap = shadowMapData,
                                                                     .color = lightColor,
                                                                     .exposure = lightPreExposureValue(),
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
                                                               .exposure = lightPreExposureValue(),
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
            forEachShadowCastingLight([&](size_t, Light& light) {
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
            cmdList.rebuildTopLevelAcceratationStructure(sceneTlas);
        }
    };
}

Model& Scene::addModel(std::unique_ptr<Model> model)
{
    ASSERT(model);
    model->setScene({}, this);
    m_models.push_back(std::move(model));
    return *m_models.back().get();
}

void Scene::setShouldMaintainRayTracingScene(bool value)
{
    // For now, only let this be changed before anything has been loaded. Also, assume 
    ASSERT(m_filePath.empty());

    m_maintainRayTracingScene = value;
}

DirectionalLight& Scene::addLight(std::unique_ptr<DirectionalLight> light)
{
    ASSERT(light);
    light->setScene({}, this);
    m_directionalLights.push_back(std::move(light));
    return *m_directionalLights.back().get();
}

SpotLight& Scene::addLight(std::unique_ptr<SpotLight> light)
{
    ASSERT(light);
    light->setScene({}, this);
    m_spotLights.push_back(std::move(light));
    return *m_spotLights.back().get();
}

size_t Scene::meshCount() const
{
    size_t count = 0u;
    for (auto& model : m_models) {
        count += model->meshCount();
    }
    return count;
}

void Scene::forEachModel(std::function<void(size_t, const Model&)> callback) const
{
    for (size_t i = 0; i < m_models.size(); ++i) {
        const Model& model = *m_models[i];
        callback(i, model);
    }
}

void Scene::forEachModel(std::function<void(size_t, Model&)> callback)
{
    for (size_t i = 0; i < m_models.size(); ++i) {
        Model& model = *m_models[i];
        callback(i, model);
    }
}

int Scene::forEachMesh(std::function<void(size_t, const Mesh&)> callback) const
{
    size_t nextIndex = 0;
    for (auto& model : m_models) {
        model->forEachMesh([&](const Mesh& mesh) {
            callback(nextIndex++, mesh);
        });
    }
    return static_cast<int>(nextIndex);
}

int Scene::forEachMesh(std::function<void(size_t, Mesh&)> callback)
{
    size_t nextIndex = 0;
    for (auto& model : m_models) {
        model->forEachMesh([&](Mesh& mesh) {
            callback(nextIndex++, mesh);
        });
    }
    return static_cast<int>(nextIndex);
}

int Scene::forEachLight(std::function<void(size_t, const Light&)> callback) const
{
    size_t nextIndex = 0;
    for (auto& light : m_directionalLights) {
        callback(nextIndex++, *light);
    }
    for (auto& light : m_spotLights) {
        callback(nextIndex++, *light);
    }
    return static_cast<int>(nextIndex);
}

int Scene::forEachLight(std::function<void(size_t, Light&)> callback)
{
    size_t nextIndex = 0;
    for (auto& light : m_directionalLights) {
        callback(nextIndex++, *light);
    }
    for (auto& light : m_spotLights) {
        callback(nextIndex++, *light);
    }
    return static_cast<int>(nextIndex);
}

int Scene::forEachShadowCastingLight(std::function<void(size_t, const Light&)> callback) const
{
    size_t nextIndex = 0;
    for (auto& light : m_directionalLights) {
        if (light->castsShadows())
            callback(nextIndex++, *light);
    }
    for (auto& light : m_spotLights) {
        if (light->castsShadows())
            callback(nextIndex++, *light);
    }
    return static_cast<int>(nextIndex);
}

int Scene::forEachShadowCastingLight(std::function<void(size_t, Light&)> callback)
{
    size_t nextIndex = 0;
    for (auto& light : m_directionalLights) {
        if (light->castsShadows())
            callback(nextIndex++, *light);
    }
    for (auto& light : m_spotLights) {
        if (light->castsShadows())
            callback(nextIndex++, *light);
    }
    return static_cast<int>(nextIndex);
}

void Scene::generateProbeGridFromBoundingBox()
{
    NOT_YET_IMPLEMENTED();

    constexpr int maxGridSideSize = 16;
    constexpr float boxPadding = 0.0f;

    moos::aabb3 sceneBox {};
    forEachMesh([&](size_t, Mesh& mesh) {
        // TODO: Transform the bounding box first, obviously..
        // But we aren't using this path right now so not going
        // to spend time on it right now.
        moos::aabb3 meshBox = mesh.boundingBox();
        sceneBox.expandWithPoint(meshBox.min);
        sceneBox.expandWithPoint(meshBox.max);
    });
    sceneBox.min -= vec3(boxPadding);
    sceneBox.max += vec3(boxPadding);

    vec3 dims = sceneBox.max - sceneBox.min;
    int counts[3] = { maxGridSideSize, maxGridSideSize, maxGridSideSize };
    int indexOfSmallest = 0;
    if (dims.y < dims.x || dims.z < dims.x) {
        if (dims.y < dims.z) {
            indexOfSmallest = 1;
        } else {
            indexOfSmallest = 2;
        }
    }
    counts[indexOfSmallest] /= 2;

    vec3 spacing = dims / vec3((float)counts[0], (float)counts[1], (float)counts[2]);

    ProbeGrid grid;
    grid.offsetToFirst = sceneBox.min;
    grid.gridDimensions = Extent3D(counts[0], counts[1], counts[2]);
    grid.probeSpacing = spacing;
    setProbeGrid(grid);
}

DrawCallDescription Scene::fitVertexAndIndexDataForMesh(Badge<Mesh>, const Mesh& mesh, const VertexLayout& layout, std::optional<DrawCallDescription> alignWith)
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

        Buffer& buffer = m_registry.createBuffer(std::max(initialVertexBufferSize, minRequiredBufferSize), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        buffer.setName("SceneVertexBuffer");

        m_globalVertexBuffers[layout] = &buffer;
    }

    Buffer& vertexBuffer = *m_globalVertexBuffers[layout];
    size_t newDataStartOffset = doAlign
        ? alignWith->vertexOffset * layout.packedVertexSize()
        : m_nextFreeVertexIndex * layout.packedVertexSize();

    vertexBuffer.updateDataAndGrowIfRequired(vertexData.data(), vertexData.size(), newDataStartOffset);

    if (doAlign) {
        // TODO: Maybe ensure we haven't already fitted this mesh+layout combo and is just overwriting at this point. Well, before doing it I guess..
        DrawCallDescription reusedDrawCall = alignWith.value();
        reusedDrawCall.vertexBuffer = m_globalVertexBuffers[layout];
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
            m_global32BitIndexBuffer = &m_registry.createBuffer(std::max(initialIndexBufferSize, requiredAdditionalSize), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
            m_global32BitIndexBuffer->setName("SceneIndexBuffer");
        }

        uint32_t firstIndex = m_nextFreeIndex;
        m_nextFreeIndex += (uint32_t)indexData.size();

        m_global32BitIndexBuffer->updateDataAndGrowIfRequired(indexData.data(), requiredAdditionalSize, firstIndex * sizeof(uint32_t));

        drawCall.indexBuffer = m_global32BitIndexBuffer;
        drawCall.indexCount = (uint32_t)indexData.size();
        drawCall.indexType = IndexType::UInt32;
        drawCall.firstIndex = firstIndex;
    }

    return drawCall;
}

Buffer& Scene::globalVertexBufferForLayout(const VertexLayout& layout) const
{
    auto entry = m_globalVertexBuffers.find(layout);
    if (entry == m_globalVertexBuffers.end())
        LogErrorAndExit("Can't get vertex buffer for layout since it has not been created! Please ensureDrawCallIsAvailable for at least one mesh before calling this.\n");
    return *entry->second;
}

Buffer& Scene::globalIndexBuffer() const
{
    if (m_global32BitIndexBuffer == nullptr)
        LogErrorAndExit("Can't get global index buffer since it has not been created! Please ensureDrawCallIsAvailable for at least one indexed mesh before calling this.\n");
    return *m_global32BitIndexBuffer;
}

IndexType Scene::globalIndexBufferType() const
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

void Scene::rebuildGpuSceneData()
{
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

    int numMeshes = forEachMesh([&](size_t meshIdx, Mesh& mesh) {

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

            // TODO: Probably create a geometry per mesh but only a single instance per model, and use the SBT for material lookup!
            RTGeometryInstance instance = { .blas = m_registry.createBottomLevelAccelerationStructure({ geometry }),
                                            .transform = mesh.model()->transform(),
                                            .shaderBindingTableOffset = 0, // todo: generalize!
                                            .customInstanceId = static_cast<uint32_t>(meshIdx),
                                            .hitMask = hitMask };

            m_rayTracingGeometryInstances.push_back(instance);
        }

    });

    if (m_usedTextures.size() > SCENE_MAX_TEXTURES) {
        LogErrorAndExit("Scene: we need to up the number of max textures that can be handled by the scene! We have %u, the capacity is %u.\n",
                        m_usedTextures.size(), SCENE_MAX_TEXTURES);
    }

    // Create material buffer
    size_t materialBufferSize = m_usedMaterials.size() * sizeof(ShaderMaterial);
    m_materialDataBuffer = &m_registry.createBuffer(materialBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_materialDataBuffer->updateData(m_usedMaterials.data(), materialBufferSize);
    m_materialDataBuffer->setName("SceneMaterialData");

    m_materialBindingSet = &m_registry.createBindingSet({ { 0, ShaderStage::Any, m_materialDataBuffer },
                                                          { 1, ShaderStage::Any, m_usedTextures, SCENE_MAX_TEXTURES } });
    m_materialBindingSet->setName("SceneMaterialSet");

    if (doesMaintainRayTracingScene() && m_rayTracingGeometryInstances.size() > 0) {
        // TODO: If we call rebuildGpuSceneData twice or more we will leak the TLAS!
        m_sceneTopLevelAccelerationStructure = &m_registry.createTopLevelAccelerationStructure(m_rayTracingGeometryInstances);
    }
}

BindingSet& Scene::globalMaterialBindingSet() const
{
    ASSERT(m_materialBindingSet);
    return *m_materialBindingSet;
}

TopLevelAS& Scene::globalTopLevelAccelerationStructure() const
{
    ASSERT(doesMaintainRayTracingScene());
    ASSERT(m_sceneTopLevelAccelerationStructure);
    return *m_sceneTopLevelAccelerationStructure;
}

float Scene::filmGrainGain() const
{
    return m_fixedFilmGrainGain;
}

void Scene::drawSceneGui()
{
    ImGui::Begin("Scene");

    {
        ImGui::Text("Number of managed resources:");
        ImGui::Columns(3);
        ImGui::Text("meshes: %u", meshCount());
        ImGui::NextColumn();
        ImGui::Text("materials: %u", m_usedMaterials.size());
        ImGui::NextColumn();
        ImGui::Text("textures: %u", m_usedTextures.size());
        ImGui::Columns(1);
    }

    ImGui::Separator();

    if (ImGui::TreeNode("Film grain")) {
        // TODO: I would love to estimate gain grain from ISO and scene light amount, but that's for later..
        ImGui::SliderFloat("Fixed grain gain", &m_fixedFilmGrainGain, 0.0f, 0.25f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Environment")) {
        ImGui::SliderFloat("Ambient (lx)", &m_ambientIlluminance, 0.0f, 1'000.0f, "%.0f");
        // NOTE: Obviously the unit of this is dependent on the values in the texture.. we should probably unify this a bit.
        ImGui::SliderFloat("Environment multiplier", &m_environmentMultiplier, 0.0f, 10'000.0f, "%.0f");
        ImGui::TreePop();
    }

    ImGui::Separator();

    {
        static Light* selectedLight = nullptr;
        if (ImGui::BeginCombo("Inspected light", selectedLight ? selectedLight->name().c_str() : "Select a light")) {
            forEachLight([&](size_t lightIndex, Light& light) {
                bool selected = &light == selectedLight;
                if (ImGui::Selectable(light.name().c_str(), &selected))
                    selectedLight = &light;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            });
            ImGui::EndCombo();
        }

        if (selectedLight != nullptr) {

            ImGui::ColorEdit3("Color", value_ptr(selectedLight->color));

            switch (selectedLight->type()) {
            case Light::Type::DirectionalLight:
                ImGui::SliderFloat("Illuminance (lx)", &static_cast<DirectionalLight*>(selectedLight)->illuminance, 1.0f, 150000.0f);
                break;
            case Light::Type::SpotLight:
                ImGui::SliderFloat("Luminous intensity (cd)", &static_cast<SpotLight*>(selectedLight)->luminousIntensity, 1.0f, 1000.0f);
                break;
            case Light::Type::PointLight:
                break;
            default:
                ASSERT_NOT_REACHED();
            }

            ImGui::SliderFloat("Constant bias", &selectedLight->customConstantBias, 0.0f, 20.0f);
            ImGui::SliderFloat("Slope bias", &selectedLight->customSlopeBias, 0.0f, 10.0f);
        }
    }

    ImGui::Separator();

    if (ImGui::TreeNode("Exposure control")) {
        camera().renderExposureGUI();
        ImGui::TreePop();
    }

    if (ImGui::Button("Copy current camera to clipboard")) {
        saveCameraToClipboard(*m_currentMainCamera);
    }

    ImGui::End();
}

void Scene::drawSceneGizmos()
{
    static ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;

    auto& input = Input::instance();
    if (input.wasKeyPressed(Key::T))
        operation = ImGuizmo::TRANSLATE;
    else if (input.wasKeyPressed(Key::R))
        operation = ImGuizmo::ROTATE;
    else if (input.wasKeyPressed(Key::Y))
        operation = ImGuizmo::SCALE;

    if (selectedModel()) {

        ImGuizmo::BeginFrame();
        ImGuizmo::SetRect(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

        // FIXME: Support world transforms! Well, we don't really have hierarchies right now, so it doesn't really matter.
        //  What we do have is meshes with their own transform under a model, and we are modifying the model's transform here.
        //  Maybe in the future we want to be able to modify meshes too?
        ImGuizmo::MODE mode = ImGuizmo::LOCAL;

        mat4 viewMatrix = camera().viewMatrix();
        mat4 projMatrix = camera().projectionMatrix();

        // Silly stuff, since ImGuizmo doesn't seem to like my projection matrix..
        projMatrix.y = -projMatrix.y;

        mat4 matrix = selectedModel()->transform().localMatrix();
        ImGuizmo::Manipulate(value_ptr(viewMatrix), value_ptr(projMatrix), operation, mode, value_ptr(matrix));
        selectedModel()->transform().setLocalMatrix(matrix);
    }
}
