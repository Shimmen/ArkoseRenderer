#include "GpuScene.h"

#include "backend/Resources.h"
#include "core/Conversion.h"
#include "core/Logging.h"
#include "core/parallel/TaskGraph.h"
#include "rendering/Registry.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <ark/aabb.h>
#include <ark/transform.h>

// Shared shader headers
using uint = uint32_t;
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
    m_lightGrayTexture = Texture::createFromPixel(backend(), vec4(0.75f, 0.75f, 0.75f, 1.0f), true);
    m_magentaTexture = Texture::createFromPixel(backend(), vec4(1.0f, 0.0f, 1.0f, 1.0f), true);
    m_normalMapBlueTexture = Texture::createFromPixel(backend(), vec4(0.5f, 0.5f, 1.0f, 1.0f), false);

    size_t materialBufferSize = MaxSupportedSceneMaterials * sizeof(ShaderMaterial);
    m_materialDataBuffer = backend().createBuffer(materialBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_materialDataBuffer->setName("SceneMaterialData");

    // TODO: Get rid of this placeholder that we use to write into all texture slots (i.e. support partially bound etc.)
    std::vector<Texture*> placeholderTexture = { m_magentaTexture.get() };
    m_materialBindingSet = backend().createBindingSet({ ShaderBinding::storageBuffer(*m_materialDataBuffer.get()),
                                                        ShaderBinding::sampledTextureBindlessArray(MaxSupportedSceneTextures, placeholderTexture) });
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
    return m_managedDirectionalLights.size() + m_managedSpotLights.size();
}

size_t GpuScene::shadowCastingLightCount() const
{
    // eh, i'm lazy
    return forEachShadowCastingLight([](size_t, const Light&) {});
}

size_t GpuScene::forEachShadowCastingLight(std::function<void(size_t, Light&)> callback)
{
    size_t nextIndex = 0;
    for (auto& managedLight : m_managedDirectionalLights) {
        if (managedLight.light->castsShadows())
            callback(nextIndex++, *managedLight.light);
    }
    for (auto& managedLight : m_managedSpotLights) {
        if (managedLight.light->castsShadows())
            callback(nextIndex++, *managedLight.light);
    }
    return nextIndex;
}

size_t GpuScene::forEachShadowCastingLight(std::function<void(size_t, const Light&)> callback) const
{
    size_t nextIndex = 0;
    for (auto& managedLight : m_managedDirectionalLights) {
        if (managedLight.light->castsShadows())
            callback(nextIndex++, *managedLight.light);
    }
    for (auto& managedLight : m_managedSpotLights) {
        if (managedLight.light->castsShadows())
            callback(nextIndex++, *managedLight.light);
    }
    return nextIndex;
}

size_t GpuScene::forEachLocalLight(std::function<void(size_t, Light&)> callback)
{
    size_t nextIndex = 0;
    for (auto& managedLight : m_managedSpotLights) {
        callback(nextIndex++, *managedLight.light);
    }
    return nextIndex;
}
size_t GpuScene::forEachLocalLight(std::function<void(size_t, const Light&)> callback) const
{
    size_t nextIndex = 0;
    for (auto& managedLight : m_managedSpotLights) {
        callback(nextIndex++, *managedLight.light);
    }
    return nextIndex;
}

RenderPipelineNode::ExecuteCallback GpuScene::construct(GpuScene&, Registry& reg)
{
    // G-Buffer textures
    {
        Extent2D windowExtent = reg.windowRenderTarget().extent();

        auto nearestFilter = Texture::Filters::nearest();
        auto linerFilter = Texture::Filters::linear();
        auto mipMode = Texture::Mipmap::None;
        auto wrapMode = Texture::WrapModes::clampAllToEdge();

        Texture& depthTexture = reg.createTexture2D(windowExtent, Texture::Format::Depth24Stencil8, nearestFilter, mipMode, wrapMode);
        reg.publish("SceneDepth", depthTexture);

        // rgb: scene color, a: unused
        Texture& colorTexture = reg.createTexture2D(windowExtent, Texture::Format::RGBA16F, linerFilter, mipMode, wrapMode);
        reg.publish("SceneColor", colorTexture);

        // rg: encoded normal, ba: velocity in image plane (2D)
        Texture& normalVelocityTexture = reg.createTexture2D(windowExtent, Texture::Format::RGBA16F, linerFilter, mipMode, wrapMode);
        reg.publish("SceneNormalVelocity", normalVelocityTexture);

        // r: roughness, g: metallic, b: unused, a: unused
        Texture& materialTexture = reg.createTexture2D(windowExtent, Texture::Format::RGBA16F, linerFilter, mipMode, wrapMode);
        reg.publish("SceneMaterial", materialTexture);

        // rgb: base color, a: unused
        Texture& baseColorTexture = reg.createTexture2D(windowExtent, Texture::Format::RGBA8, linerFilter, mipMode, wrapMode);
        reg.publish("SceneBaseColor", baseColorTexture);

        // rgb: diffuse color, a: unused
        Texture& diffueGiTexture = reg.createTexture2D(windowExtent, Texture::Format::RGBA16F, linerFilter, mipMode, wrapMode);
        reg.publish("DiffuseGI", diffueGiTexture);
    }

    Buffer& cameraBuffer = reg.createBuffer(sizeof(CameraState), Buffer::Usage::ConstantBuffer, Buffer::MemoryHint::GpuOnly);
    BindingSet& cameraBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(cameraBuffer, ShaderStage::AnyRasterize) });
    reg.publish("SceneCameraData", cameraBuffer);
    reg.publish("SceneCameraSet", cameraBindingSet);

    // Object data stuff
    // TODO: Resize the buffer if needed when more meshes are added
    size_t objectDataBufferSize = meshCount() * sizeof(ShaderDrawable);
    Buffer& objectDataBuffer = reg.createBuffer(objectDataBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    objectDataBuffer.setName("SceneObjectData");
    BindingSet& objectBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(objectDataBuffer, ShaderStage::Vertex) });
    reg.publish("SceneObjectSet", objectBindingSet);

    if (m_maintainRayTracingScene) {

        // TODO: Make buffer big enough to contain all meshes we may want
        Buffer& meshBuffer = reg.createBuffer(m_rayTracingMeshData, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
        BindingSet& rtMeshDataBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(meshBuffer, ShaderStage::AnyRayTrace),
                                                                  ShaderBinding::storageBuffer(globalIndexBuffer(), ShaderStage::AnyRayTrace),
                                                                  ShaderBinding::storageBuffer(globalVertexBufferForLayout(m_rayTracingVertexLayout), ShaderStage::AnyRayTrace) });

        reg.publish("SceneRTMeshDataSet", rtMeshDataBindingSet);
    }

    // Light shadow data stuff (todo: make not fixed!)
    size_t numShadowCastingLights = shadowCastingLightCount();
    Buffer& lightShadowDataBuffer = reg.createBuffer(numShadowCastingLights * sizeof(PerLightShadowData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("SceneShadowData", lightShadowDataBuffer);

    // Light data stuff
    Buffer& lightMetaDataBuffer = reg.createBuffer(sizeof(LightMetaData), Buffer::Usage::ConstantBuffer, Buffer::MemoryHint::GpuOnly);
    lightMetaDataBuffer.setName("SceneLightMetaData");
    Buffer& dirLightDataBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    dirLightDataBuffer.setName("SceneDirectionalLightData");
    Buffer& spotLightDataBuffer = reg.createBuffer(10 * sizeof(SpotLightData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    spotLightDataBuffer.setName("SceneSpotLightData");

    BindingSet& lightBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(lightMetaDataBuffer),
                                                         ShaderBinding::storageBuffer(dirLightDataBuffer),
                                                         ShaderBinding::storageBuffer(spotLightDataBuffer) });
    reg.publish("SceneLightSet", lightBindingSet);

    // Misc. data
    Texture& blueNoiseTextureArray = reg.loadTextureArrayFromFileSequence("assets/blue-noise/64_64/HDR_RGBA_{}.png", false, false);
    reg.publish("BlueNoise", blueNoiseTextureArray);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        // If we're using async texture updates, create textures for the images we've now loaded in
        // TODO: Also create the texture and set the data asynchronously so we avoid practically all stalls
        if (m_asyncLoadedImages.size() > 0) {
            SCOPED_PROFILE_ZONE_NAMED("Finalizing async-loaded images")
            std::scoped_lock<std::mutex> lock { m_asyncLoadedImagesMutex };

            size_t numToFinalize = std::min(MaxNumAsyncTextureLoadsToFinalizePerFrame, m_asyncLoadedImages.size());
            for (size_t i = 0; i < numToFinalize; ++i) {
                const LoadedImageForTextureCreation& loadedImageForTex = m_asyncLoadedImages[i];

                auto texture = backend().createTexture(loadedImageForTex.textureDescription);
                texture->setData(loadedImageForTex.image->data(), loadedImageForTex.image->size());
                texture->setName("Texture:" + loadedImageForTex.path);
                m_managedTexturesVramUsage += texture->sizeInMemory();

                updateTexture(loadedImageForTex.textureHandle, std::move(texture));
            }
            m_asyncLoadedImages.erase(m_asyncLoadedImages.begin(), m_asyncLoadedImages.begin() + numToFinalize);
        }

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

                .focalLength = camera.focalLengthMeters(),

                .iso = camera.ISO(),
                .aperture = camera.fNumber(),
                .shutterSpeed = camera.shutterSpeed(),
                .exposureCompensation = camera.exposureCompensation(),
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
        // NOTE: If auto exposure we can't treat the value as-is since it's from the previous frame!
        m_lightPreExposure = camera().exposure();

        // Update light data
        {
            mat4 viewFromWorld = camera().viewMatrix();
            mat4 worldFromView = inverse(viewFromWorld);

            int nextShadowMapIndex = 0;
            std::vector<DirectionalLightData> dirLightData;
            std::vector<SpotLightData> spotLightData;

            for (const ManagedDirectionalLight& managedLight : m_managedDirectionalLights) {

                if (!managedLight.light) {
                    continue;
                }

                const DirectionalLight& light = *managedLight.light;

                int shadowMapIndex = light.castsShadows() ? nextShadowMapIndex++ : -1;
                ShadowMapData shadowMapData { .textureIndex = shadowMapIndex };

                vec3 lightColor = light.color * light.intensityValue() * lightPreExposure();

                dirLightData.emplace_back(DirectionalLightData { .shadowMap = shadowMapData,
                                                                 .color = lightColor,
                                                                 .exposure = lightPreExposure(),
                                                                 .worldSpaceDirection = vec4(normalize(light.forwardDirection()), 0.0),
                                                                 .viewSpaceDirection = viewFromWorld * vec4(normalize(light.forwardDirection()), 0.0),
                                                                 .lightProjectionFromWorld = light.viewProjection(),
                                                                 .lightProjectionFromView = light.viewProjection() * worldFromView });
            }

            for (const ManagedSpotLight& managedLight : m_managedSpotLights) {

                if (!managedLight.light) {
                    continue;
                }

                const SpotLight& light = *managedLight.light;

                int shadowMapIndex = light.castsShadows() ? nextShadowMapIndex++ : -1;
                ShadowMapData shadowMapData { .textureIndex = shadowMapIndex };

                vec3 lightColor = light.color * light.intensityValue() * lightPreExposure();

                    spotLightData.emplace_back(SpotLightData { .shadowMap = shadowMapData,
                                                           .color = lightColor,
                                                           .exposure = lightPreExposure(),
                                                           .worldSpaceDirection = vec4(normalize(light.forwardDirection()), 0.0f),
                                                           .viewSpaceDirection = viewFromWorld * vec4(normalize(light.forwardDirection()), 0.0f),
                                                           .lightProjectionFromWorld = light.viewProjection(),
                                                           .lightProjectionFromView = light.viewProjection() * worldFromView,
                                                           .worldSpacePosition = vec4(light.position(), 0.0f),
                                                           .viewSpacePosition = viewFromWorld * vec4(light.position(), 1.0f),
                                                           .outerConeHalfAngle = light.outerConeAngle / 2.0f,
                                                           .iesProfileIndex = managedLight.iesLut.indexOfType<int>(),
                                                           ._pad0 = vec2() });
            }

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
    SCOPED_PROFILE_ZONE();

    m_environmentMapTexture = environmentMap.assetPath.empty()
        ? Texture::createFromPixel(backend(), vec4(1.0f), true)
        : Texture::createFromImagePath(backend(), environmentMap.assetPath, true, false, Texture::WrapModes::repeatAll());
}

Texture& GpuScene::environmentMapTexture()
{
    ARKOSE_ASSERT(m_environmentMapTexture);
    return *m_environmentMapTexture;
}

void GpuScene::registerLight(SpotLight& light)
{
    TextureHandle iesLutHandle {};
    if (light.hasIesProfile()) {
        auto iesLut = light.iesProfile().createLookupTexture(backend(), SpotLight::IESLookupTextureSize);
        iesLut->setName("IES-LUT:" + light.iesProfile().path());
        iesLutHandle = registerTexture(std::move(iesLut));
    }

    TextureHandle shadowMapHandle {};
    if (light.castsShadows()) {
        auto shadowMap = createShadowMap(light);
        shadowMapHandle = registerTexture(std::move(shadowMap));
    }

    ManagedSpotLight managedLight { .light = &light,
                                    .iesLut = iesLutHandle,
                                    .shadowMapTex = shadowMapHandle };

    m_managedSpotLights.push_back(managedLight);
}

void GpuScene::registerLight(DirectionalLight& light)
{
    TextureHandle shadowMapHandle {};
    if (light.castsShadows()) {
        auto shadowMap = createShadowMap(light);
        shadowMapHandle = registerTexture(std::move(shadowMap));
    }

    ManagedDirectionalLight managedLight { .light = &light,
                                           .shadowMapTex = shadowMapHandle };

    m_managedDirectionalLights.push_back(managedLight);
}

void GpuScene::registerMesh(Mesh& mesh)
{
    SCOPED_PROFILE_ZONE();

    m_managedMeshes.push_back(&mesh);

    Material& material = mesh.material();
    MaterialHandle materialHandle = registerMaterial(material);
    ARKOSE_ASSERT(materialHandle.valid());

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
    ARKOSE_ASSERT(drawCallDesc.type == DrawCallDescription::Type ::Indexed);

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
    ARKOSE_ASSERT(hitMask != 0);

    m_sceneBottomLevelAccelerationStructures.emplace_back(backend().createBottomLevelAccelerationStructure({ geometry }));
    BottomLevelAS& blas = *m_sceneBottomLevelAccelerationStructures.back();
    m_totalBlasVramUsage += blas.sizeInMemory();

    // TODO: Probably create a geometry per mesh but only a single instance per model, and use the SBT for material lookup!
    RTGeometryInstance instance = { .blas = blas,
                                    .transform = mesh.model()->transform(),
                                    .shaderBindingTableOffset = 0, // todo: generalize!
                                    .customInstanceId = meshIdx,
                                    .hitMask = hitMask };

    return instance;
}

std::unique_ptr<Texture> GpuScene::createShadowMap(const Light& light)
{
    ARKOSE_ASSERT(light.shadowMapSize().width() > 0);
    ARKOSE_ASSERT(light.shadowMapSize().height() > 0);

    Texture::Description textureDesc { .type = Texture::Type::Texture2D,
                                       .arrayCount = 1,

                                       .extent = Extent3D(light.shadowMapSize()),
                                       .format = Texture::Format::Depth32F,

                                       .filter = Texture::Filters::linear(),
                                       .wrapMode = Texture::WrapModes::clampAllToEdge(),

                                       .mipmap = Texture::Mipmap::None,
                                       .multisampling = Texture::Multisampling::None };

    auto shadowMapTex = backend().createTexture(textureDesc);
    shadowMapTex->setName(light.name() + "ShadowMap");

    return shadowMapTex;
}

MaterialHandle GpuScene::registerMaterial(Material& material)
{
    SCOPED_PROFILE_ZONE();

    // NOTE: A material here is very lightweight (for now) so we don't cache them

    // Register textures
    TextureHandle baseColor = registerMaterialTexture(material.baseColor);
    TextureHandle emissive = registerMaterialTexture(material.emissive);
    TextureHandle normalMap = registerMaterialTexture(material.normalMap);
    TextureHandle metallicRoughness = registerMaterialTexture(material.metallicRoughness);

    ShaderMaterial shaderMaterial {};

    shaderMaterial.baseColor = baseColor.indexOfType<int>();
    shaderMaterial.normalMap = normalMap.indexOfType<int>();
    shaderMaterial.metallicRoughness = metallicRoughness.indexOfType<int>();
    shaderMaterial.emissive = emissive.indexOfType<int>();

    shaderMaterial.blendMode = material.blendModeValue();
    shaderMaterial.maskCutoff = material.maskCutoff;

    uint64_t materialIdx = m_managedMaterials.size();
    if (materialIdx >= MaxSupportedSceneMaterials) {
        ARKOSE_LOG(Fatal, "Ran out of managed scene materials, exiting.");
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

    ARKOSE_ASSERT(handle.valid());
    ARKOSE_ASSERT(handle.index() < m_managedMaterials.size());

    ManagedMaterial& managedMaterial = m_managedMaterials[handle.index()];
    ARKOSE_ASSERT(managedMaterial.referenceCount == 1); // (for now, only a single ref.)
    managedMaterial.referenceCount -= 1;

    // TODO: Manage a free-list of indices to reuse!

    m_pendingMaterialUpdates.push_back(handle.indexOfType<uint32_t>());

    if (managedMaterial.referenceCount == 0) {
        // TODO: Put this handle in some handle free list for index reuse so we don't leave gaps
        managedMaterial = ManagedMaterial();
    }
}

TextureHandle GpuScene::registerMaterialTexture(Material::TextureDescription& description)
{
    SCOPED_PROFILE_ZONE();

    auto entry = m_materialTextureCache.find(description);
    if (entry == m_materialTextureCache.end()) {

        ARKOSE_LOG(Verbose, "GPUScene: Registering new material texture: {}", description.toString());

        auto createTextureFromMaterialTextureDesc = [](Backend& backend, Material::TextureDescription desc) -> std::unique_ptr<Texture> {
            if (desc.hasImage()) {
                return Texture::createFromImage(backend, desc.image.value(), desc.sRGB, desc.mipmapped, desc.wrapMode);
            } else if (desc.hasPath()) {
                return Texture::createFromImagePath(backend, desc.path, desc.sRGB, desc.mipmapped, desc.wrapMode);
            } else {
                return Texture::createFromPixel(backend, desc.fallbackColor, desc.sRGB);
            }
        };

        TextureHandle handle = registerTextureSlot();
        m_materialTextureCache[description] = handle;

        // TODO: Right now we defer the final step, i.e. making a Texture from the loaded image, back to the main thread,
        // so we should only do the loading in the async path. However, if we include the Texture creation in the async path
        // it would make sense to also load the image-based Textures here. (Also, in most cases it's just paths.)
        if (UseAsyncTextureLoads && description.hasPath()) {

            // Put some placeholder texture for this texture slot before the async has loaded in fully
            // TODO: Instead of guessing, maybe let the description describe what type of content we have (e.g. normal map)?
            {
                const vec4& pixelColor = description.fallbackColor;
                auto almostEqual = [](float a, float b) -> bool { return std::abs(a - b) < 1e-2f; };
                if (almostEqual(pixelColor.x, 0.5f) && almostEqual(pixelColor.y, 0.5f) && almostEqual(pixelColor.z, 1.0f) && almostEqual(pixelColor.w, 1.0f)) {
                    updateTextureUnowned(handle, m_normalMapBlueTexture.get());
                } else {
                    updateTextureUnowned(handle, m_lightGrayTexture.get());
                }
            }

            TaskGraph::get().enqueueTask([this, description, handle]() {

                Image::Info* info = Image::getInfo(description.path);
                if (!info) {
                    ARKOSE_LOG(Fatal, "GpuScene: could not read image '{}', exiting", description.path);
                }

                Texture::Format format;
                Image::PixelType pixelTypeToUse;
                Texture::pixelFormatAndTypeForImageInfo(*info, description.sRGB, format, pixelTypeToUse);

                auto mipmapMode = (description.mipmapped && info->width > 1 && info->height > 1)
                    ? Texture::Mipmap::Linear
                    : Texture::Mipmap::None;

                Texture::Description desc {
                    .type = Texture::Type::Texture2D,
                    .arrayCount = 1u,
                    .extent = { (uint32_t)info->width, (uint32_t)info->height, 1 },
                    .format = format,
                    .filter = Texture::Filters::linear(),
                    .wrapMode = Texture::WrapModes::repeatAll(),
                    .mipmap = mipmapMode,
                    .multisampling = Texture::Multisampling::None
                };

                Image* image = Image::load(description.path, pixelTypeToUse, true);

                {
                    SCOPED_PROFILE_ZONE_NAMED("Pushing async-loaded image")
                    std::scoped_lock<std::mutex> lock { m_asyncLoadedImagesMutex };
                    m_asyncLoadedImages.push_back(LoadedImageForTextureCreation { .image = image,
                                                                                  .path = description.path,
                                                                                  .textureHandle = handle,
                                                                                  .textureDescription = desc });
                }
            });

        } else {
            auto texture = createTextureFromMaterialTextureDesc(backend(), description);
            m_managedTexturesVramUsage += texture->sizeInMemory();
            updateTexture(handle, std::move(texture));
        }

        return handle;
    }

    TextureHandle handle = entry->second;
    ARKOSE_ASSERT(handle.valid());

    ARKOSE_ASSERT(handle.index() < m_managedTextures.size());
    ManagedTexture& managedTexture = m_managedTextures[handle.index()];
    managedTexture.referenceCount += 1;

    return handle;
}

TextureHandle GpuScene::registerTexture(std::unique_ptr<Texture>&& texture)
{
    SCOPED_PROFILE_ZONE();

    m_managedTexturesVramUsage += texture->sizeInMemory();

    TextureHandle handle = registerTextureSlot();
    updateTexture(handle, std::move(texture));

    return handle;
}

TextureHandle GpuScene::registerTextureSlot()
{
    uint64_t textureIdx = m_managedTextures.size();
    if (textureIdx >= MaxSupportedSceneTextures) {
        ARKOSE_LOG(Fatal, "Ran out of bindless scene texture slots, exiting.");
    }

    auto handle = TextureHandle(textureIdx);

    m_managedTextures.push_back(ManagedTexture { .texture = nullptr,
                                                 .referenceCount = 1 });

    return handle;
}

void GpuScene::updateTexture(TextureHandle handle, std::unique_ptr<Texture>&& texture)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(handle.valid());
    ARKOSE_ASSERT(handle.index() < m_managedTextures.size());

    auto index = handle.indexOfType<uint32_t>();
    ManagedTexture& managedTexture = m_managedTextures[index];

    // TODO: What if the managed texture is deleted between now and the pending update? We need to protect against that!
    // One way would be to just put in the index in here and then when it's time to actually update, put in the texture pointer.

    // TODO: Pending texture updates should be unique for an index! Only use the latest texture for a given index! Even better,
    // why not just keep a single index to update here and we'll always use the managedTexture's texture for that index. The only
    // problem is that our current API doesn't know about managedTextures, so would need to convert to what the API accepts.

    managedTexture.texture = std::move(texture);
    m_pendingTextureUpdates.push_back({ .texture = managedTexture.texture.get(),
                                        .index = index });
}

void GpuScene::updateTextureUnowned(TextureHandle handle, Texture* texture)
{
    ARKOSE_ASSERT(handle.valid());
    ARKOSE_ASSERT(handle.index() < m_managedTextures.size());

    // TODO: If we have the same handle twice, probably remove/overwrite the first one! We don't want to send more updates than needed.
    // We could use a set (hashed on index) and always overwrite? Or eliminate duplicates at final step (see updateTexture comment above).

    auto index = handle.indexOfType<uint32_t>();
    m_pendingTextureUpdates.push_back({ .texture = texture,
                                        .index = index });
}

void GpuScene::unregisterTexture(TextureHandle handle)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(handle.valid());
    ARKOSE_ASSERT(handle.index() < m_managedTextures.size());

    ManagedTexture& managedTexture = m_managedTextures[handle.index()];
    ARKOSE_ASSERT(managedTexture.referenceCount > 0);
    managedTexture.referenceCount -= 1;

    // TODO: Manage a free-list of indices to reuse!

    // Write symbolic blank texture to the index
    m_pendingTextureUpdates.push_back({ .texture = m_magentaTexture.get(),
                                        .index = handle.indexOfType<uint32_t>() });

    if (managedTexture.referenceCount == 0) {

        ARKOSE_ASSERT(m_managedTexturesVramUsage > managedTexture.texture->sizeInMemory());
        m_managedTexturesVramUsage -= managedTexture.texture->sizeInMemory();

        // TODO: Put this handle in some handle free list for index reuse so we don't leave gaps
        managedTexture = ManagedTexture();
    }
}

DrawCallDescription GpuScene::fitVertexAndIndexDataForMesh(Badge<Mesh>, const Mesh& mesh, const VertexLayout& layout, std::optional<DrawCallDescription> alignWith)
{
    const size_t initialIndexBufferSize = 100'000 * sizeofIndexType(globalIndexBufferType());
    const size_t initialVertexBufferSize = 50'000 * layout.packedVertexSize();

    bool doAlign = alignWith.has_value();
    ARKOSE_ASSERT(!alignWith || alignWith->sourceMesh == &mesh);

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
        ARKOSE_LOG(Fatal, "Can't get vertex buffer for layout since it has not been created! Please ensureDrawCallIsAvailable for at least one mesh before calling this.");
    return *entry->second;
}

Buffer& GpuScene::globalIndexBuffer() const
{
    if (m_global32BitIndexBuffer == nullptr)
        ARKOSE_LOG(Fatal, "Can't get global index buffer since it has not been created! Please ensureDrawCallIsAvailable for at least one indexed mesh before calling this.");
    return *m_global32BitIndexBuffer;
}

IndexType GpuScene::globalIndexBufferType() const
{
    // For simplicity we keep a single 32-bit index buffer, since every mesh should fit in there.
    return IndexType::UInt32;
}

BindingSet& GpuScene::globalMaterialBindingSet() const
{
    ARKOSE_ASSERT(m_materialBindingSet);
    return *m_materialBindingSet;
}

TopLevelAS& GpuScene::globalTopLevelAccelerationStructure() const
{
    ARKOSE_ASSERT(m_maintainRayTracingScene);
    ARKOSE_ASSERT(m_sceneTopLevelAccelerationStructure);
    return *m_sceneTopLevelAccelerationStructure;
}

void GpuScene::drawStatsGui(bool includeContainingWindow)
{
    if (includeContainingWindow) {
        ImGui::Begin("GPU Scene");
    }

    ImGui::Text("Number of managed resources:");
    ImGui::Columns(3);
    ImGui::Text("meshes: %u", m_managedMeshes.size());
    ImGui::NextColumn();
    ImGui::Text("materials: %u", m_managedMaterials.size());
    ImGui::NextColumn();
    ImGui::Text("textures: %u", m_managedTextures.size());
    ImGui::Columns(1);

    if (includeContainingWindow) {
        ImGui::End();
    }
}

void GpuScene::drawVramUsageGui(bool includeContainingWindow)
{
    if (includeContainingWindow) {
        ImGui::Begin("VRAM usage");
    }

    if (backend().vramStatsReportRate() > 0 && backend().vramStats().has_value()) {

        VramStats stats = backend().vramStats().value();

        float currentTotalUsedGB = conversion::to::GB(stats.totalUsed);
        ImGui::Text("Current VRAM usage: %.2f GB", currentTotalUsedGB);

        for (size_t heapIdx = 0, heapCount = stats.heaps.size(); heapIdx < heapCount; ++heapIdx) {
            if (heapIdx >= m_vramUsageHistoryPerHeap.size()) {
                m_vramUsageHistoryPerHeap.resize(heapIdx + 1);
            }
            if (ImGui::GetFrameCount() % backend().vramStatsReportRate() == 0) {
                float heapUsedMB = conversion::to::MB(stats.heaps[heapIdx].used);
                m_vramUsageHistoryPerHeap[heapIdx].report(heapUsedMB);
            }
        }

        std::vector<std::string> heapNames {};
        if (ImGui::BeginTable("MeshVertexDataVramUsageTable", 5)) {

            ImGui::TableSetupColumn("Heap", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Used / Available (MB)", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Device local", ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableSetupColumn("Host visible", ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableSetupColumn("Host coherent", ImGuiTableColumnFlags_WidthFixed, 100.0f);

            ImGui::TableHeadersRow();

            for (size_t heapIdx = 0, heapCount = stats.heaps.size(); heapIdx < heapCount; ++heapIdx) {
                VramStats::MemoryHeap& heap = stats.heaps[heapIdx];

                float filledPercentage = static_cast<float>(heap.used) / static_cast<float>(heap.available);
                ImVec4 textColor = ImColor(0.2f, 1.0f, 0.2f);
                if (filledPercentage >= 0.99f) {
                    textColor = ImColor(1.0f, 0.2f, 0.2f);
                } else if (filledPercentage > 0.85f) {
                    textColor = ImColor(1.0f, 0.65f, 0.0f);
                }

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                heapNames.push_back(fmt::format("Heap{}", heapIdx));
                ImGui::Text(heapNames.back().c_str());

                ImGui::TableSetColumnIndex(1);
                float heapUsedMB = conversion::to::MB(heap.used);
                float heapAvailableMB = conversion::to::MB(heap.available);
                ImGui::TextColored(textColor, "%.1f / %.1f", heapUsedMB, heapAvailableMB);


                ImGui::TableSetColumnIndex(2);
                ImGui::Text(heap.deviceLocal ? "x" : "");

                ImGui::TableSetColumnIndex(3);
                ImGui::Text(heap.hostVisible ? "x" : "");

                ImGui::TableSetColumnIndex(4);
                ImGui::Text(heap.hostCoherent ? "x" : "");
            }

            ImGui::EndTable();
        }

        if (ImGui::BeginTabBar("VramGraphsTabBar")) {
            for (int i = 0; i < stats.heaps.size(); ++i) {
                if (ImGui::BeginTabItem(heapNames[i].c_str())) {

                    auto valuesGetter = [](void* data, int idx) -> float {
                        const auto& avgAccumulator = *reinterpret_cast<VramUsageAvgAccumulatorType*>(data);
                        return static_cast<float>(avgAccumulator.valueAtSequentialIndex(idx));
                    };

                    int valuesCount = static_cast<int>(VramUsageAvgAccumulatorType::RunningAvgWindowSize);
                    float heapAvailableMB = conversion::to::MB(stats.heaps[i].available);
                    ImVec2 plotSize = ImVec2(ImGui::GetContentRegionAvail().x, 200.0f);
                    ImGui::PlotLines("##VramUsagePlotPerHeap", valuesGetter, (void*)&m_vramUsageHistoryPerHeap[i], valuesCount, 0, "VRAM (MB)", 0.0f, heapAvailableMB, plotSize);

                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

    } else {
        ImGui::Text("(No VRAM usage data provided by the backend)");
    }

    ImGui::Separator();

    if (ImGui::BeginTabBar("VramUsageBreakdown")) {

        if (ImGui::BeginTabItem("Managed textures")) {

            ImGui::Text("Number of managed textures: %d", m_managedTextures.size());

            float managedTexturesTotalGB = conversion::to::GB(m_managedTexturesVramUsage);
            ImGui::Text("Using %.2f GB", managedTexturesTotalGB);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Mesh index data")) {

            ImGui::Text("Using global index type %s", indexTypeToString(globalIndexBufferType()));

            float allocatedSizeMB = conversion::to::MB(globalIndexBuffer().sizeInMemory());
            float usedSizeMB = conversion::to::MB(m_nextFreeIndex * sizeofIndexType(globalIndexBufferType()));
            ImGui::Text("Using %.1f MB (%.1f MB allocated)", usedSizeMB, allocatedSizeMB);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Mesh vertex data")) {

            float totalAllocatedSizeMB = 0.0f;
            float totalUsedSizeMB = 0.0f;

            if (ImGui::BeginTable("MeshVertexDataVramUsageTable", 3)) {

                ImGui::TableSetupColumn("Vertex layout", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Used size (MB)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Allocated size (MB)", ImGuiTableColumnFlags_WidthFixed, 140.0f);

                ImGui::TableHeadersRow();

                for (auto& [vertexLayout, buffer] : m_globalVertexBuffers) {

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    std::string layoutDescription = vertexLayout.toString(false);
                    ImGui::Text(layoutDescription.c_str());
                        
                    ImGui::TableSetColumnIndex(1);
                    float usedSizeMB = conversion::to::MB(m_nextFreeVertexIndex * vertexLayout.packedVertexSize());
                    ImGui::Text("%.2f", usedSizeMB);

                    ImGui::TableSetColumnIndex(2);
                    float allocatedSizeMB = conversion::to::MB(buffer->sizeInMemory());
                    ImGui::Text("%.2f", allocatedSizeMB);

                    totalAllocatedSizeMB += allocatedSizeMB;
                    totalUsedSizeMB += usedSizeMB;
                }

                ImGui::EndTable();
            }

            ImGui::Separator();
            ImGui::Text("Total: %.2f MB (%.2f MB allocated)", totalUsedSizeMB, totalAllocatedSizeMB);

            ImGui::EndTabItem();
        }

        if (m_maintainRayTracingScene && ImGui::BeginTabItem("Ray Tracing BLAS")) {

            size_t numBLAS = m_sceneBottomLevelAccelerationStructures.size();
            ImGui::Text("Number of BLASs: %d", numBLAS);

            float blasTotalSizeMB = conversion::to::MB(m_totalBlasVramUsage);
            ImGui::Text("BLAS total usage: %.2f MB", blasTotalSizeMB);

            float blasAverageSizeMB = blasTotalSizeMB / numBLAS;
            ImGui::Text("Average per BLAS: %.2f MB", blasAverageSizeMB);

            ImGui::Separator();

            std::string layoutDescription = m_rayTracingVertexLayout.toString(false);
            ImGui::Text("Using vertex layout: [ %s ]", layoutDescription.c_str());
            ImGui::TextColored(ImColor(0.75f, 0.75f, 0.75f), "(Note: This vertex data does not count to the BLAS size)");

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (includeContainingWindow) {
        ImGui::End();
    }
}

