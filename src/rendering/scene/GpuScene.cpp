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

GpuScene::GpuScene(Scene& scene, Backend& backend, Extent2D initialMainViewportSize)
    : m_scene(scene)
    , m_backend(backend)
{
}

void GpuScene::initialize(Badge<Scene>, bool rayTracingCapable)
{
    m_maintainRayTracingScene = rayTracingCapable;

    m_blackTexture = Texture::createFromPixel(backend(), vec4(0.0f, 0.0f, 0.0f, 0.0f), true);
    m_magentaTexture = Texture::createFromPixel(backend(), vec4(1.0f, 0.0f, 1.0f, 1.0f), true);
    m_normalMapBlueTexture = Texture::createFromPixel(backend(), vec4(0.5f, 0.5f, 1.0f, 1.0f), false);

    size_t materialBufferSize = MaxSupportedSceneMaterials * sizeof(ShaderMaterial);
    m_materialDataBuffer = backend().createBuffer(materialBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_materialDataBuffer->setName("SceneMaterialData");

    // TODO: Get rid of this placeholder that we use to write into all texture slots (i.e. support partially bound etc.)
    std::vector<Texture*> placeholderTexture = { m_magentaTexture.get() };
    m_materialBindingSet = backend().createBindingSet({ { MaterialBindingSetBindingIndexMaterials, ShaderStage::Any, m_materialDataBuffer.get() },
                                                        { MaterialBindingSetBindingIndexTextures, ShaderStage::Any, MaxSupportedSceneTextures, placeholderTexture } });
    m_materialBindingSet->setName("SceneMaterialSet");

    if (m_maintainRayTracingScene) {
        m_sceneTopLevelAccelerationStructure = backend().createTopLevelAccelerationStructure(InitialMaxRayTracingGeometryInstanceCount, {});
    }
}

size_t GpuScene::forEachMesh(std::function<void(size_t, Mesh&)> callback)
{
    size_t nextIndex = 0;
    for (Mesh* mesh : m_managedMeshes) {
        callback(nextIndex++, *mesh);
    }
    return nextIndex;
}

size_t GpuScene::forEachMesh(std::function<void(size_t, const Mesh&)> callback) const
{
    size_t nextIndex = 0;
    for (const Mesh* mesh : m_managedMeshes) {
        callback(nextIndex++, *mesh);
    }
    return nextIndex;
}

size_t GpuScene::lightCount() const
{
    return m_directionalLights.size() + m_spotLights.size();
}

size_t GpuScene::forEachLight(std::function<void(size_t, Light&)> callback)
{
    size_t nextIndex = 0;
    for (auto& light : m_directionalLights) {
        callback(nextIndex++, *light);
    }
    for (auto& light : m_spotLights) {
        callback(nextIndex++, *light);
    }
    return nextIndex;
}

size_t GpuScene::forEachLight(std::function<void(size_t, const Light&)> callback) const
{
    size_t nextIndex = 0;
    for (const auto& light : m_directionalLights) {
        callback(nextIndex++, *light);
    }
    for (const auto& light : m_spotLights) {
        callback(nextIndex++, *light);
    }
    return nextIndex;
}

size_t GpuScene::shadowCastingLightCount() const
{
    // eh, i'm lazy
    return forEachShadowCastingLight([](size_t, const Light&) {});
}

size_t GpuScene::forEachShadowCastingLight(std::function<void(size_t, Light&)> callback)
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
    return nextIndex;
}

size_t GpuScene::forEachShadowCastingLight(std::function<void(size_t, const Light&)> callback) const
{
    size_t nextIndex = 0;
    for (auto* light : m_directionalLights) {
        if (light->castsShadows())
            callback(nextIndex++, *light);
    }
    for (auto* light : m_spotLights) {
        if (light->castsShadows())
            callback(nextIndex++, *light);
    }
    return nextIndex;
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
    size_t objectDataBufferSize = meshCount() * sizeof(ShaderDrawable);
    Buffer& objectDataBuffer = reg.createBuffer(objectDataBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    objectDataBuffer.setName("SceneObjectData");
    BindingSet& objectBindingSet = reg.createBindingSet({ { 0, ShaderStage::Vertex, &objectDataBuffer } });
    reg.publish("objectSet", objectBindingSet);

    if (m_maintainRayTracingScene) {

        // TODO: Make buffer big enough to contain all meshes we may want
        Buffer& meshBuffer = reg.createBuffer(m_rayTracingMeshData, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
        BindingSet& rtMeshDataBindingSet = reg.createBindingSet({ { 0, ShaderStage::AnyRayTrace, &meshBuffer },
                                                                  { 1, ShaderStage::AnyRayTrace, &globalIndexBuffer() },
                                                                  { 2, ShaderStage::AnyRayTrace, &globalVertexBufferForLayout(m_rayTracingVertexLayout) } });

        reg.publish("SceneRTMeshDataSet", rtMeshDataBindingSet);
    }

    // Light shadow data stuff (todo: make not fixed!)
    size_t numShadowCastingLights = shadowCastingLightCount();
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

        // Update bindless textures
        if (m_pendingTextureUpdates.size() > 0) {
            m_materialBindingSet->updateTextures(MaterialBindingSetBindingIndexTextures, m_pendingTextureUpdates);
            m_pendingTextureUpdates.clear();
        }

        // Update material data
        if (m_pendingMaterialUpdates.size() > 0)
        {
            // TODO: Probably batch all neighbouring indices into a single upload? (Or can we let the UploadBuffer do that optimization for us?)
            for (uint32_t materialIdx : m_pendingMaterialUpdates) {
                const ShaderMaterial& shaderMaterial = m_managedMaterials[materialIdx].material;
                size_t bufferOffset = materialIdx * sizeof(ShaderMaterial);
                uploadBuffer.upload(shaderMaterial, *m_materialDataBuffer, bufferOffset);
            }
            m_pendingMaterialUpdates.clear();
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
            for (int i = 0; i < meshCount(); ++i) {

                const Mesh& mesh = *m_managedMeshes[i];
                ShaderDrawable& drawable = m_rasterizerMeshData[i];

                drawable.worldFromLocal = mesh.transform().worldMatrix();
                drawable.worldFromTangent = mat4(mesh.transform().worldNormalMatrix());
                drawable.previousFrameWorldFromLocal = mesh.transform().previousFrameWorldMatrix();
            }

            uploadBuffer.upload(m_rasterizerMeshData, objectDataBuffer);
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
            forEachShadowCastingLight([&](size_t, Light& light) {
                shadowData.push_back({ .lightViewFromWorld = light.lightViewMatrix(),
                                       .lightProjectionFromWorld = light.viewProjection(),
                                       .constantBias = light.constantBias(),
                                       .slopeBias = light.slopeBias() });
            });
            uploadBuffer.upload(shadowData, lightShadowDataBuffer);
        }

        cmdList.executeBufferCopyOperations(uploadBuffer);

        if (m_maintainRayTracingScene) {

            TopLevelAS& sceneTlas = *m_sceneTopLevelAccelerationStructure;

            sceneTlas.updateInstanceDataWithUploadBuffer(m_rayTracingGeometryInstances, uploadBuffer);
            cmdList.executeBufferCopyOperations(uploadBuffer);

            // Only do an update most frame, but every x frames require a full rebuild
            auto buildType = AccelerationStructureBuildType::Update;
            if (m_framesUntilNextFullTlasBuild == 0) {
                buildType = AccelerationStructureBuildType::FullBuild;
                m_framesUntilNextFullTlasBuild = 60;
            }
            
            cmdList.buildTopLevelAcceratationStructure(sceneTlas, buildType);
            m_framesUntilNextFullTlasBuild -= 1;
        }
    };
}

void GpuScene::updateEnvironmentMap(Scene::EnvironmentMap& environmentMap)
{
    m_environmentMapTexture = environmentMap.assetPath.empty()
        ? Texture::createFromPixel(backend(), vec4(1.0f), true)
        : Texture::createFromImagePath(backend(), environmentMap.assetPath, true, false, Texture::WrapModes::repeatAll());
}

Texture& GpuScene::environmentMapTexture()
{
    ASSERT(m_environmentMapTexture);
    return *m_environmentMapTexture;
}

void GpuScene::registerLight(SpotLight& light)
{
    m_spotLights.push_back(&light);
}

void GpuScene::registerLight(DirectionalLight& light)
{
    m_directionalLights.push_back(&light);
}

void GpuScene::registerMesh(Mesh& mesh)
{
    SCOPED_PROFILE_ZONE();

    m_managedMeshes.push_back(&mesh);

    Material& material = mesh.material();
    MaterialHandle materialHandle = registerMaterial(material);
    ASSERT(materialHandle.valid());

    // TODO: This is the legacy path, get rid of it! CullingNode still uses it directly, but I am not so sure it should..
    mesh.setMaterialIndex({}, materialHandle.indexOfType<int>());

    // NOTE: Matrices are set at "render-time" before each frame starts
    ShaderDrawable shaderDrawable;
    shaderDrawable.materialIndex = materialHandle.indexOfType<int>();
    m_rasterizerMeshData.push_back(shaderDrawable);

    if (m_maintainRayTracingScene) {

        uint32_t rtMeshIndex = static_cast<uint32_t>(m_rayTracingMeshData.size());

        const DrawCallDescription& drawCallDesc = mesh.drawCallDescription(m_rayTracingVertexLayout, *this);
        m_rayTracingMeshData.push_back(RTTriangleMesh { .firstVertex = drawCallDesc.vertexOffset,
                                                        .firstIndex = (int32_t)drawCallDesc.firstIndex,
                                                        .materialIndex = materialHandle.indexOfType<int>() });

        RTGeometryInstance rtGeometryInstance = createRTGeometryInstance(mesh, rtMeshIndex);
        m_rayTracingGeometryInstances.push_back(rtGeometryInstance);
    }
}

RTGeometryInstance GpuScene::createRTGeometryInstance(Mesh& mesh, uint32_t meshIdx)
{
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
    switch (mesh.material().blendMode) {
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

    m_sceneBottomLevelAccelerationStructures.emplace_back(backend().createBottomLevelAccelerationStructure({ geometry }));
    BottomLevelAS& blas = *m_sceneBottomLevelAccelerationStructures.back();

    // TODO: Probably create a geometry per mesh but only a single instance per model, and use the SBT for material lookup!
    RTGeometryInstance instance = { .blas = blas,
                                    .transform = mesh.model()->transform(),
                                    .shaderBindingTableOffset = 0, // todo: generalize!
                                    .customInstanceId = meshIdx,
                                    .hitMask = hitMask };

    return instance;
}

MaterialHandle GpuScene::registerMaterial(Material& material)
{
    SCOPED_PROFILE_ZONE();

    // NOTE: A material here is very lightweight (for now) so we don't cache them

    // Register textures
    TextureHandle baseColor = registerTexture(material.baseColor);
    TextureHandle emissive = registerTexture(material.emissive);
    TextureHandle normalMap = registerTexture(material.normalMap);
    TextureHandle metallicRoughness = registerTexture(material.metallicRoughness);

    ShaderMaterial shaderMaterial {};

    shaderMaterial.baseColor = baseColor.indexOfType<int>();
    shaderMaterial.normalMap = normalMap.indexOfType<int>();
    shaderMaterial.metallicRoughness = metallicRoughness.indexOfType<int>();
    shaderMaterial.emissive = emissive.indexOfType<int>();

    shaderMaterial.blendMode = material.blendModeValue();
    shaderMaterial.maskCutoff = material.maskCutoff;

    uint64_t materialIdx = m_managedMaterials.size();
    if (materialIdx >= MaxSupportedSceneMaterials) {
        LogErrorAndExit("Ran out of managed scene materials, exiting.\n");
    }

    auto handle = MaterialHandle(materialIdx);

    m_managedMaterials.push_back(ManagedMaterial { .material = shaderMaterial,
                                                   .referenceCount = 1 });

    m_pendingMaterialUpdates.push_back(handle.indexOfType<uint32_t>());

    return handle;
}

void GpuScene::unregisterMaterial(MaterialHandle handle)
{
    SCOPED_PROFILE_ZONE();

    ASSERT(handle.valid());
    ASSERT(handle.index() < m_managedMaterials.size());

    ManagedMaterial& managedMaterial = m_managedMaterials[handle.index()];
    ASSERT(managedMaterial.referenceCount == 1); // (for now, only a single ref.)
    managedMaterial.referenceCount -= 1;

    // TODO: Manage a free-list of indices to reuse!

    m_pendingMaterialUpdates.push_back(handle.indexOfType<uint32_t>());

    if (managedMaterial.referenceCount == 0) {
        // TODO: Put this handle in some handle free list for index reuse so we don't leave gaps
        managedMaterial = ManagedMaterial();
    }
}

TextureHandle GpuScene::registerTexture(Material::TextureDescription& desc)
{
    SCOPED_PROFILE_ZONE();

    auto entry = m_textureCache.find(desc);
    if (entry == m_textureCache.end()) {

        std::unique_ptr<Texture> texture {};
        if (desc.hasImage()) {
            texture = Texture::createFromImage(backend(), desc.image.value(), desc.sRGB, desc.mipmapped, desc.wrapMode);
        } else if (desc.hasPath()) {
            texture = Texture::createFromImagePath(backend(), desc.path, desc.sRGB, desc.mipmapped, desc.wrapMode);
        } else {
            // TODO: Maybe keep a cache of pixel textures so we don't create so many:
            // NOTE: If we support HDR/float colors for pixel textures, this key won't be enough..
            //  But right now we still only have RGBA8 or sRGBA8 pixel textures, so this is fine.
            //uint32_t key = uint32_t(255.99f * moos::clamp(color.x, 0.0f, 1.0f))
            //    | (uint32_t(255.99f * moos::clamp(color.y, 0.0f, 1.0f)) << 8)
            //    | (uint32_t(255.99f * moos::clamp(color.z, 0.0f, 1.0f)) << 16)
            //    | (uint32_t(255.99f * moos::clamp(color.w, 0.0f, 1.0f)) << 24);
            texture = Texture::createFromPixel(backend(), desc.fallbackColor, desc.sRGB);
        }

        uint64_t textureIdx = m_managedTextures.size();
        if (textureIdx >= MaxSupportedSceneTextures) {
            LogErrorAndExit("Ran out of bindless scene texture slots, exiting.\n");
        }

        auto handle = TextureHandle(textureIdx);
        m_textureCache[desc] = handle;

        m_pendingTextureUpdates.push_back({ .texture = texture.get(),
                                            .index = handle.indexOfType<uint32_t>() });

        m_managedTextures.push_back(ManagedTexture { .texture = std::move(texture),
                                                     .description = desc,
                                                     .referenceCount = 1 });

        return handle;
    }

    TextureHandle handle = entry->second;
    ASSERT(handle.valid());

    ASSERT(handle.index() < m_managedTextures.size());
    ManagedTexture& managedTexture = m_managedTextures[handle.index()];
    managedTexture.referenceCount += 1;

    return handle;
}

void GpuScene::unregisterTexture(TextureHandle handle)
{
    SCOPED_PROFILE_ZONE();

    ASSERT(handle.valid());
    ASSERT(handle.index() < m_managedTextures.size());

    ManagedTexture& managedTexture = m_managedTextures[handle.index()];
    ASSERT(managedTexture.referenceCount > 0);
    managedTexture.referenceCount -= 1;

    // TODO: Manage a free-list of indices to reuse!

    // Write symbolic blank texture to the index
    m_pendingTextureUpdates.push_back({ .texture = m_magentaTexture.get(),
                                        .index = handle.indexOfType<uint32_t>() });

    if (managedTexture.referenceCount == 0) {
        // TODO: Put this handle in some handle free list for index reuse so we don't leave gaps
        managedTexture = ManagedTexture();
    }
}

DrawCallDescription GpuScene::fitVertexAndIndexDataForMesh(Badge<Mesh>, const Mesh& mesh, const VertexLayout& layout, std::optional<DrawCallDescription> alignWith)
{
    const size_t initialIndexBufferSize = 100'000 * sizeofIndexType(globalIndexBufferType());
    const size_t initialVertexBufferSize = 50'000 * layout.packedVertexSize();

    bool doAlign = alignWith.has_value();
    ASSERT(!alignWith || alignWith->sourceMesh == &mesh);

    std::vector<uint8_t> vertexData = mesh.vertexData(layout);

    auto entry = m_globalVertexBuffers.find(layout);
    if (entry == m_globalVertexBuffers.end()) {

        size_t offset = doAlign ? (alignWith->vertexOffset * layout.packedVertexSize()) : 0;
        size_t minRequiredBufferSize = offset + vertexData.size();

        m_globalVertexBuffers[layout] = backend().createBuffer(std::max(initialVertexBufferSize, minRequiredBufferSize), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
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
            m_global32BitIndexBuffer = backend().createBuffer(std::max(initialIndexBufferSize, requiredAdditionalSize), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
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

BindingSet& GpuScene::globalMaterialBindingSet() const
{
    ASSERT(m_materialBindingSet);
    return *m_materialBindingSet;
}

TopLevelAS& GpuScene::globalTopLevelAccelerationStructure() const
{
    ASSERT(m_maintainRayTracingScene);
    ASSERT(m_sceneTopLevelAccelerationStructure);
    return *m_sceneTopLevelAccelerationStructure;
}

void GpuScene::drawGui()
{
    ImGui::Begin("GPU Scene");

    ImGui::Text("Number of managed resources:");
    ImGui::Columns(3);
    ImGui::Text("meshes: %u", m_managedMeshes.size());
    ImGui::NextColumn();
    ImGui::Text("materials: %u", m_managedMaterials.size());
    ImGui::NextColumn();
    ImGui::Text("textures: %u", m_managedTextures.size());
    ImGui::Columns(1);

    ImGui::End();
}
