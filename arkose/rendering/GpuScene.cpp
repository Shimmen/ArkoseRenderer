#include "GpuScene.h"

#include "asset/ImageAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/StaticMeshAsset.h"
#include "rendering/backend/Resources.h"
#include "core/Conversion.h"
#include "core/Logging.h"
#include "core/Types.h"
#include "core/parallel/TaskGraph.h"
#include "rendering/Registry.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <ark/aabb.h>
#include <ark/transform.h>

// Shared shader headers
#include "shaders/shared/CameraState.h"

GpuScene::GpuScene(Scene& scene, Backend& backend, Extent2D initialMainViewportSize)
    : m_scene(scene)
    , m_backend(backend)
{
}

void GpuScene::initialize(Badge<Scene>, bool rayTracingCapable)
{
    m_maintainRayTracingScene = rayTracingCapable;

    m_emptyVertexBuffer = backend().createBuffer(1, Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
    m_emptyIndexBuffer = backend().createBuffer(1, Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);

    m_blackTexture = Texture::createFromPixel(backend(), vec4(0.0f, 0.0f, 0.0f, 0.0f), true);
    m_whiteTexture = Texture::createFromPixel(backend(), vec4(1.0f, 1.0f, 1.0f, 1.0f), true);
    m_lightGrayTexture = Texture::createFromPixel(backend(), vec4(0.75f, 0.75f, 0.75f, 1.0f), true);
    m_magentaTexture = Texture::createFromPixel(backend(), vec4(1.0f, 0.0f, 1.0f, 1.0f), true);
    m_normalMapBlueTexture = Texture::createFromPixel(backend(), vec4(0.5f, 0.5f, 1.0f, 1.0f), false);

    m_iconManager = std::make_unique<IconManager>(backend());

    size_t materialBufferSize = m_managedMaterials.capacity() * sizeof(ShaderMaterial);
    m_materialDataBuffer = backend().createBuffer(materialBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_materialDataBuffer->setName("SceneMaterialData");

    MaterialAsset defaultMaterialAsset {};
    defaultMaterialAsset.colorTint = vec4(1.0f, 0.0f, 1.0f, 1.0f);
    m_defaultMaterialHandle = registerMaterial(&defaultMaterialAsset);

    // TODO: Get rid of this placeholder that we use to write into all texture slots (i.e. support partially bound etc.)
    std::vector<Texture*> placeholderTexture = { m_magentaTexture.get() };
    m_materialBindingSet = backend().createBindingSet({ ShaderBinding::storageBuffer(*m_materialDataBuffer.get()),
                                                        ShaderBinding::sampledTextureBindlessArray(static_cast<uint32_t>(m_managedMaterials.capacity()), placeholderTexture) });
    m_materialBindingSet->setName("SceneMaterialSet");

    if (m_maintainRayTracingScene) {
        m_sceneTopLevelAccelerationStructure = backend().createTopLevelAccelerationStructure(InitialMaxRayTracingGeometryInstanceCount, {});
    }

    if constexpr (UseMeshletRendering) {
        m_meshletManager = std::make_unique<MeshletManager>(m_backend);
    }
}

StaticMesh* GpuScene::staticMeshForHandle(StaticMeshHandle handle)
{
    return handle.valid() ? m_managedStaticMeshes.get(handle).staticMesh.get() : nullptr;
}

const StaticMesh* GpuScene::staticMeshForHandle(StaticMeshHandle handle) const
{
    return handle.valid() ? m_managedStaticMeshes.get(handle).staticMesh.get() : nullptr;
}

const ShaderMaterial* GpuScene::materialForHandle(MaterialHandle handle) const
{
    return handle.valid() ? &m_managedMaterials.get(handle) : nullptr;
}

size_t GpuScene::lightCount() const
{
    return m_managedDirectionalLights.size() + m_managedSphereLights.size() + m_managedSpotLights.size();
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
    for (auto& managedLight : m_managedSphereLights) {
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
    for (auto& managedLight : m_managedSphereLights) {
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
        auto wrapMode = ImageWrapModes::clampAllToEdge();

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
    BindingSet& cameraBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(cameraBuffer, ShaderStage::Any) });
    reg.publish("SceneCameraData", cameraBuffer);
    reg.publish("SceneCameraSet", cameraBindingSet);

    // Object data stuff
    // TODO: Resize the buffer if needed when more meshes are added, OR crash hard
    // TODO: Make a more reasonable default too... we need: #meshes * #LODs * #segments-per-lod
    size_t objectDataBufferSize = 10'000 * sizeof(ShaderDrawable);
    Buffer& objectDataBuffer = reg.createBuffer(objectDataBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("SceneObjectData", objectDataBuffer);
    BindingSet& objectBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(objectDataBuffer, ShaderStage::Vertex) });
    reg.publish("SceneObjectSet", objectBindingSet);


    // TODO: My lambda-system kind of fails horribly here. I need a reference-type for the capture to work nicely,
    //       and I also want to scope it under the if check. I either need to fix that or I'll need to make a pointer
    //       for it and then explicitly capture that pointer for this to work.
    Buffer* rtTriangleMeshBufferPtr = nullptr;
    if (m_maintainRayTracingScene) {
        ensureDrawCallIsAvailableForAll(m_rayTracingVertexLayout);

        // TODO: Resize the buffer if needed when more meshes are added, OR crash hard
        // TODO: Make a more reasonable default too... we need: #meshes * #LODs * #segments-per-lod
        Buffer& rtTriangleMeshBuffer = reg.createBuffer(10'000 * sizeof(RTTriangleMesh), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
        rtTriangleMeshBufferPtr = &rtTriangleMeshBuffer;

        BindingSet& rtMeshDataBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(rtTriangleMeshBuffer, ShaderStage::AnyRayTrace),
                                                                  ShaderBinding::storageBuffer(globalIndexBuffer(), ShaderStage::AnyRayTrace),
                                                                  ShaderBinding::storageBuffer(globalVertexBufferForLayout(m_rayTracingVertexLayout), ShaderStage::AnyRayTrace) });
        reg.publish("SceneRTMeshDataSet", rtMeshDataBindingSet);
    }

    // Light data stuff
    Buffer& lightMetaDataBuffer = reg.createBuffer(sizeof(LightMetaData), Buffer::Usage::ConstantBuffer, Buffer::MemoryHint::GpuOnly);
    lightMetaDataBuffer.setName("SceneLightMetaData");
    Buffer& dirLightDataBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    dirLightDataBuffer.setName("SceneDirectionalLightData");
    Buffer& sphereLightDataBuffer = reg.createBuffer(10 * sizeof(SphereLightData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    sphereLightDataBuffer.setName("SceneSphereLightData");
    Buffer& spotLightDataBuffer = reg.createBuffer(10 * sizeof(SpotLightData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    spotLightDataBuffer.setName("SceneSpotLightData");

    BindingSet& lightBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(lightMetaDataBuffer),
                                                         ShaderBinding::storageBuffer(dirLightDataBuffer),
                                                         ShaderBinding::storageBuffer(sphereLightDataBuffer),
                                                         ShaderBinding::storageBuffer(spotLightDataBuffer) });
    reg.publish("SceneLightSet", lightBindingSet);

    // Misc. data
    Texture& blueNoiseTextureArray = reg.loadTextureArrayFromFileSequence("assets/blue-noise/64_64/HDR_RGBA_{}.png", false, false);
    reg.publish("BlueNoise", blueNoiseTextureArray);

    return [&, rtTriangleMeshBufferPtr](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        SCOPED_PROFILE_ZONE_NAMED("GpuScene update");

        m_currentFrameIdx = appState.frameIndex();
        processDeferredDeletions();

        // If we're using async texture updates, create textures for the images we've now loaded in
        // TODO: Also create the texture and set the data asynchronously so we avoid practically all stalls
        if (m_asyncLoadedImages.size() > 0) {
            SCOPED_PROFILE_ZONE_NAMED("Finalizing async-loaded images");
            std::scoped_lock<std::mutex> lock { m_asyncLoadedImagesMutex };

            // Use up to 75% of the upload buffer's total size for streaming texture uploads
            const size_t textureUploadBudget = static_cast<size_t>(0.75f * uploadBuffer.size());
            size_t remainingTextureUploadBudget = textureUploadBudget;

            std::vector<Texture*> texturesNeedingGeneratedMips {};

            size_t numUploadedTextures = 0;
            while (numUploadedTextures < m_asyncLoadedImages.size()) {
                const LoadedImageForTextureCreation& loadedImageForTex = m_asyncLoadedImages[numUploadedTextures];

                ARKOSE_ASSERT(loadedImageForTex.imageAsset->numMips() > 0);
                bool assetHasMips = loadedImageForTex.imageAsset->numMips() > 1;
                bool textureWantMips = loadedImageForTex.textureDescription.mipmap != Texture::Mipmap::None;

                size_t sizeToUpload = (textureWantMips)
                    ? loadedImageForTex.imageAsset->totalImageSizeIncludingMips()
                    : loadedImageForTex.imageAsset->pixelDataForMip(0).size();

                if (sizeToUpload > remainingTextureUploadBudget) {
                    if (sizeToUpload > textureUploadBudget) {
                        ARKOSE_LOG(Fatal, "Image asset is {:.2f} MB but the texture upload budget is only {:.2f} MB. "
                                          "The budget must be increased if we want to be able to load this asset.",
                                   conversion::to::MB(sizeToUpload), conversion::to::MB(textureUploadBudget));
                    } else {
                        // Stop uploading textures now, as we've hit the budget
                        break;
                    }
                }

                auto texture = backend().createTexture(loadedImageForTex.textureDescription);
                texture->setName("Texture<" + std::string(loadedImageForTex.imageAsset->assetFilePath()) + ">");

                if (not assetHasMips || not textureWantMips) {
                    std::span<const u8> mip0PixelData = loadedImageForTex.imageAsset->pixelDataForMip(0);
                    uploadBuffer.upload(mip0PixelData.data(), mip0PixelData.size(), *texture, 0);
                }

                if (textureWantMips) {
                    if (assetHasMips) {
                        for (size_t mipIdx = 0; mipIdx < loadedImageForTex.imageAsset->numMips(); ++mipIdx) {
                            std::span<const u8> mipPixelData = loadedImageForTex.imageAsset->pixelDataForMip(mipIdx);
                            uploadBuffer.upload(mipPixelData.data(), mipPixelData.size(), *texture, mipIdx);
                        }
                    } else {
                        // Needs to be done after buffer upload copy operations are completed
                        texturesNeedingGeneratedMips.push_back(texture.get());
                    }
                }

                m_managedTexturesVramUsage += texture->sizeInMemory();
                updateTexture(loadedImageForTex.textureHandle, std::move(texture));

                numUploadedTextures += 1;
                ARKOSE_ASSERT(sizeToUpload <= remainingTextureUploadBudget);
                remainingTextureUploadBudget -= sizeToUpload;
            }

            if (numUploadedTextures > 0) {
                m_asyncLoadedImages.erase(m_asyncLoadedImages.begin(), m_asyncLoadedImages.begin() + numUploadedTextures);

                cmdList.executeBufferCopyOperations(uploadBuffer);
                for (Texture* texture : texturesNeedingGeneratedMips) {
                    cmdList.generateMipmaps(*texture);
                }
            }
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
            for (MaterialHandle handle : m_pendingMaterialUpdates) {
                ShaderMaterial shaderMaterial = m_managedMaterials.isValidHandle(handle)
                    ? m_managedMaterials.get(handle)
                    : ShaderMaterial(); // if deleted
                size_t bufferOffset = handle.index() * sizeof(ShaderMaterial);
                uploadBuffer.upload(shaderMaterial, *m_materialDataBuffer, bufferOffset);
            }
            m_pendingMaterialUpdates.clear();
        }

        // Update mesh streaming (well, it's not much streaming to speak of right now, but it's the basis of something like that)
        if constexpr (UseMeshletRendering) {
            m_meshletManager->processMeshStreaming(cmdList);
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

                .zNear = camera.zNear(),
                .zFar = camera.zFar(),

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
            std::vector<ShaderDrawable> rasterizerMeshData {};

            const auto& staticMeshInstances = scene().staticMeshInstances();
            for (size_t i = 0, count = staticMeshInstances.size(); i < count; ++i) {

                const StaticMeshInstance& instance = *staticMeshInstances[i];
                const StaticMesh& staticMesh = *staticMeshForHandle(instance.mesh());

                // TODO: Make use of all LODs
                ARKOSE_ASSERT(staticMesh.numLODs() >= 1);
                const StaticMeshLOD& lod = staticMesh.lodAtIndex(0);

                for (const StaticMeshSegment& meshSegment : lod.meshSegments) {

                    ShaderDrawable drawable;
                    drawable.materialIndex = meshSegment.material.indexOfType<int>();
                    drawable.worldFromLocal = instance.transform().worldMatrix();
                    drawable.worldFromTangent = mat4(instance.transform().worldNormalMatrix());
                    drawable.previousFrameWorldFromLocal = instance.transform().previousFrameWorldMatrix();

                    if (meshSegment.meshletView) {
                        drawable.firstMeshlet = meshSegment.meshletView->firstMeshlet;
                        drawable.meshletCount = meshSegment.meshletView->meshletCount;
                    }

                    rasterizerMeshData.push_back(drawable);
                }
            }

            m_drawableCountForFrame = rasterizerMeshData.size();
            uploadBuffer.upload(rasterizerMeshData, objectDataBuffer);
        }

        // Update exposure data
        // NOTE: If auto exposure we can't treat the value as-is since it's from the previous frame!
        m_lightPreExposure = camera().exposure();

        // Update light data
        {
            mat4 viewFromWorld = camera().viewMatrix();
            mat4 worldFromView = inverse(viewFromWorld);

            std::vector<DirectionalLightData> dirLightData;
            std::vector<SphereLightData> sphereLightData;
            std::vector<SpotLightData> spotLightData;

            for (const ManagedDirectionalLight& managedLight : m_managedDirectionalLights) {

                if (!managedLight.light) {
                    continue;
                }

                const DirectionalLight& light = *managedLight.light;

                dirLightData.emplace_back(DirectionalLightData { .color = light.color() * light.intensityValue() * lightPreExposure(),
                                                                 .exposure = lightPreExposure(),
                                                                 .worldSpaceDirection = vec4(normalize(light.forwardDirection()), 0.0),
                                                                 .viewSpaceDirection = viewFromWorld * vec4(normalize(light.forwardDirection()), 0.0),
                                                                 .lightProjectionFromWorld = light.viewProjection(),
                                                                 .lightProjectionFromView = light.viewProjection() * worldFromView });
            }

            for (const ManagedSphereLight& managedLight : m_managedSphereLights) {

                if (!managedLight.light) {
                    continue;
                }

                const SphereLight& light = *managedLight.light;

                sphereLightData.emplace_back(SphereLightData { .color = light.color() * light.intensityValue() * lightPreExposure(),
                                                               .exposure = lightPreExposure(),
                                                               .worldSpacePosition = vec4(light.transform().positionInWorld(), 0.0f),
                                                               .viewSpacePosition = viewFromWorld * vec4(light.transform().positionInWorld(), 1.0f),
                                                               .lightRadius = light.lightRadius(),
                                                               .lightSourceRadius = light.lightSourceRadius() });
            }

            for (const ManagedSpotLight& managedLight : m_managedSpotLights) {

                if (!managedLight.light) {
                    continue;
                }

                const SpotLight& light = *managedLight.light;

                spotLightData.emplace_back(SpotLightData { .color = light.color() * light.intensityValue() * lightPreExposure(),
                                                           .exposure = lightPreExposure(),
                                                           .worldSpaceDirection = vec4(normalize(light.forwardDirection()), 0.0f),
                                                           .viewSpaceDirection = viewFromWorld * vec4(normalize(light.forwardDirection()), 0.0f),
                                                           .lightProjectionFromWorld = light.viewProjection(),
                                                           .lightProjectionFromView = light.viewProjection() * worldFromView,
                                                           .worldSpacePosition = vec4(light.transform().positionInWorld(), 0.0f),
                                                           .viewSpacePosition = viewFromWorld * vec4(light.transform().positionInWorld(), 1.0f),
                                                           .outerConeHalfAngle = light.outerConeAngle() / 2.0f,
                                                           .iesProfileIndex = managedLight.iesLut.indexOfType<int>(),
                                                           ._pad0 = vec2() });
            }

            uploadBuffer.upload(dirLightData, dirLightDataBuffer);
            uploadBuffer.upload(sphereLightData, sphereLightDataBuffer);
            uploadBuffer.upload(spotLightData, spotLightDataBuffer);

            LightMetaData metaData { .numDirectionalLights = narrow_cast<u32>(dirLightData.size()),
                                     .numSphereLights = narrow_cast<u32>(sphereLightData.size()),
                                     .numSpotLights = narrow_cast<u32>(spotLightData.size()) };
            uploadBuffer.upload(metaData, lightMetaDataBuffer);
        }

        cmdList.executeBufferCopyOperations(uploadBuffer);

        if (m_maintainRayTracingScene) {

            auto tlasBuildType = AccelerationStructureBuildType::Update;

            // TODO: Fill in both of these and upload to the GPU buffers. For now they will be 1:1
            std::vector<RTTriangleMesh> rayTracingMeshData {};
            std::vector<RTGeometryInstance> rayTracingGeometryInstances {};

            for (auto& instance : scene().staticMeshInstances()) {
                if (StaticMesh* staticMesh = staticMeshForHandle(instance->mesh())) {
                    for (StaticMeshLOD& staticMeshLOD : staticMesh->LODs()) {
                        for (StaticMeshSegment& meshSegment : staticMeshLOD.meshSegments) {

                            uint32_t rtMeshIndex = static_cast<uint32_t>(rayTracingMeshData.size());

                            const DrawCallDescription& drawCallDesc = meshSegment.drawCallDescription(m_rayTracingVertexLayout, *this);
                            rayTracingMeshData.push_back(RTTriangleMesh { .firstVertex = drawCallDesc.vertexOffset,
                                                                          .firstIndex = (int32_t)drawCallDesc.firstIndex,
                                                                          .materialIndex = meshSegment.material.indexOfType<int>() });

                            if (meshSegment.blas == nullptr) {
                                meshSegment.blas = createBottomLevelAccelerationStructure(meshSegment, rtMeshIndex);

                                m_totalNumBlas += 1;
                                m_totalBlasVramUsage += meshSegment.blas->sizeInMemory();

                                // We have new instances, a full build is needed
                                tlasBuildType = AccelerationStructureBuildType::FullBuild;
                            }

                            uint8_t hitMask = 0x00;
                            if (const ShaderMaterial* material = materialForHandle(meshSegment.material)) {
                                switch (material->blendMode) {
                                case BLEND_MODE_OPAQUE:
                                    hitMask = RT_HIT_MASK_OPAQUE;
                                    break;
                                case BLEND_MODE_MASKED:
                                    hitMask = RT_HIT_MASK_MASKED;
                                    break;
                                case BLEND_MODE_TRANSLUCENT:
                                    hitMask = RT_HIT_MASK_BLEND;
                                    break;
                                default:
                                    ASSERT_NOT_REACHED();
                                }
                            }
                            ARKOSE_ASSERT(hitMask != 0);

                            // TODO: Probably create a geometry per mesh but only a single instance per model, and use the SBT for material lookup!
                            rayTracingGeometryInstances.push_back(RTGeometryInstance { .blas = *meshSegment.blas,
                                                                                       .transform = instance->transform(), // NOTE: This is a reference!
                                                                                       .shaderBindingTableOffset = 0, // TODO: Generalize!
                                                                                       .customInstanceId = rtMeshIndex,
                                                                                       .hitMask = hitMask });
                        }
                    }
                }
            }

            ARKOSE_ASSERT(rtTriangleMeshBufferPtr != nullptr);
            uploadBuffer.upload(rayTracingMeshData, *rtTriangleMeshBufferPtr);

            TopLevelAS& sceneTlas = *m_sceneTopLevelAccelerationStructure;
            sceneTlas.updateInstanceDataWithUploadBuffer(rayTracingGeometryInstances, uploadBuffer);
            cmdList.executeBufferCopyOperations(uploadBuffer);

            // Only do an update most frame, but every x frames require a full rebuild
            if (m_framesUntilNextFullTlasBuild == 0) {
                tlasBuildType = AccelerationStructureBuildType::FullBuild;
            }
            if (tlasBuildType == AccelerationStructureBuildType::FullBuild) {
                m_framesUntilNextFullTlasBuild = 60;
            }

            cmdList.buildTopLevelAcceratationStructure(sceneTlas, tlasBuildType);
            m_framesUntilNextFullTlasBuild -= 1;
        }
    };
}

void GpuScene::updateEnvironmentMap(EnvironmentMap& environmentMap)
{
    SCOPED_PROFILE_ZONE();

    if (environmentMap.assetPath.empty()) {
        m_environmentMapTexture = Texture::createFromPixel(backend(), vec4(1.0f), true);
    } else {
        if (ImageAsset* imageAsset = ImageAsset::loadOrCreate(environmentMap.assetPath)) {
            ARKOSE_ASSERT(imageAsset->depth() == 1);

            Texture::Description desc { .type = Texture::Type::Texture2D,
                                        .arrayCount = 1u,
                                        .extent = { imageAsset->width(), imageAsset->height(), 1 },
                                        .format = Texture::convertImageFormatToTextureFormat(imageAsset->format(), imageAsset->type()),
                                        .filter = Texture::Filters::linear(),
                                        .wrapMode = ImageWrapModes::repeatAll(),
                                        .mipmap = Texture::Mipmap::None,
                                        .multisampling = Texture::Multisampling::None };

            m_environmentMapTexture = backend().createTexture(desc);
            m_environmentMapTexture->setData(imageAsset->pixelDataForMip(0).data(), imageAsset->pixelDataForMip(0).size(), 0);
            m_environmentMapTexture->setName("EnvironmentMap<" + environmentMap.assetPath + ">");
        }
    }
}

Texture& GpuScene::environmentMapTexture()
{
    if (not m_environmentMapTexture) {
        m_environmentMapTexture = Texture::createFromPixel(backend(), vec4(1.0f, 1.0f, 1.0f, 1.0f), true);
    }

    return *m_environmentMapTexture;
}

void GpuScene::registerLight(DirectionalLight& light)
{
    ManagedDirectionalLight managedLight { .light = &light };
    m_managedDirectionalLights.push_back(managedLight);
}

void GpuScene::registerLight(SphereLight& light)
{
    ManagedSphereLight managedLight { .light = &light };
    m_managedSphereLights.push_back(managedLight);
}

void GpuScene::registerLight(SpotLight& light)
{
    TextureHandle iesLutHandle {};
    if (light.hasIesProfile()) {
        auto iesLut = light.iesProfile().createLookupTexture(backend(), SpotLight::IESLookupTextureSize);
        iesLut->setName("IES-LUT:" + light.iesProfile().path());
        iesLutHandle = registerTexture(std::move(iesLut));
    }

    ManagedSpotLight managedLight { .light = &light,
                                    .iesLut = iesLutHandle };

    m_managedSpotLights.push_back(managedLight);
}

StaticMeshHandle GpuScene::registerStaticMesh(StaticMeshAsset const* staticMeshAsset)
{
    // TODO: Maybe do some kind of caching here, and if we're trying to add the same mesh twice just ignore it and reuse the exisiting

    SCOPED_PROFILE_ZONE();

    if (staticMeshAsset == nullptr) {
        return StaticMeshHandle();
    }

    auto entry = m_staticMeshAssetCache.find(staticMeshAsset);
    if (entry != m_staticMeshAssetCache.end()) {
        return entry->second;
    }

    auto staticMesh = std::make_unique<StaticMesh>(staticMeshAsset, [this](MaterialAsset const* materialAsset) -> MaterialHandle {
        if (materialAsset) {
            return registerMaterial(materialAsset);
        } else {
            return m_defaultMaterialHandle;
        }
    });

    if constexpr (UseMeshletRendering) {
        m_meshletManager->allocateMeshlets(*staticMesh);
    }

    StaticMeshHandle handle = m_managedStaticMeshes.add(ManagedStaticMesh { .staticMeshAsset = staticMeshAsset,
                                                                            .staticMesh = std::move(staticMesh) });

    m_staticMeshAssetCache[staticMeshAsset] = handle;

    return handle;
}

void GpuScene::unregisterStaticMesh(StaticMeshHandle handle)
{
    // Do we really want to reference count this..? Or do we want some more explicit load/unload control?
    // This way it would be easy to add some function `registerExistingStaticMesh` or so which just increments
    // the reference count and returns the same handle? Not sure if that's a good use case, but this will work
    // for now and allows us to delete unused meshes...
    m_managedStaticMeshes.removeReference(handle, m_currentFrameIdx);
}

void GpuScene::ensureDrawCallIsAvailableForAll(VertexLayout vertexLayout)
{
    // TODO: Implement iterator for ResourceList!
    m_managedStaticMeshes.forEachResource([&](ManagedStaticMesh const& managedStaticMesh) {
        if (auto& staticMesh = managedStaticMesh.staticMesh) {
            for (StaticMeshLOD& staticMeshLOD : staticMesh->LODs()) {
                for (StaticMeshSegment& meshSegment : staticMeshLOD.meshSegments) {
                    meshSegment.ensureDrawCallIsAvailable(vertexLayout, *this);
                }
            }
        }
    });
}

std::unique_ptr<BottomLevelAS> GpuScene::createBottomLevelAccelerationStructure(StaticMeshSegment& meshSegment, uint32_t meshIdx)
{
    VertexLayout vertexLayout = { VertexComponent::Position3F };
    size_t vertexStride = vertexLayout.packedVertexSize();
    RTVertexFormat vertexFormat = RTVertexFormat::XYZ32F;

    const DrawCallDescription& drawCallDesc = meshSegment.drawCallDescription(vertexLayout, *this);
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
                                  .transform = mat4(1.0f) };

    return backend().createBottomLevelAccelerationStructure({ geometry });
}

MaterialHandle GpuScene::registerMaterial(MaterialAsset const* materialAsset)
{
    SCOPED_PROFILE_ZONE();

    // NOTE: A material in this context is very lightweight (for now) so we don't cache them

    // Register textures / material inputs
    TextureHandle baseColor = registerMaterialTexture(materialAsset->baseColor, ImageType::sRGBColor, m_whiteTexture.get());
    TextureHandle emissive = registerMaterialTexture(materialAsset->emissiveColor, ImageType::sRGBColor, m_blackTexture.get());
    TextureHandle normalMap = registerMaterialTexture(materialAsset->normalMap, ImageType::NormalMap, m_normalMapBlueTexture.get());
    TextureHandle metallicRoughness = registerMaterialTexture(materialAsset->materialProperties, ImageType::GenericData, m_whiteTexture.get());

    ShaderMaterial shaderMaterial {};

    shaderMaterial.baseColor = baseColor.indexOfType<int>();
    shaderMaterial.normalMap = normalMap.indexOfType<int>();
    shaderMaterial.metallicRoughness = metallicRoughness.indexOfType<int>();
    shaderMaterial.emissive = emissive.indexOfType<int>();

    auto translateBlendModeToShaderMaterial = [](BlendMode blendMode) -> int {
        switch (blendMode) {
        case BlendMode::Opaque:
            return BLEND_MODE_OPAQUE;
        case BlendMode::Masked:
            return BLEND_MODE_MASKED;
        case BlendMode::Translucent:
            return BLEND_MODE_TRANSLUCENT;
        default:
            ASSERT_NOT_REACHED();
        }
    };

    shaderMaterial.blendMode = translateBlendModeToShaderMaterial(materialAsset->blendMode);
    shaderMaterial.maskCutoff = materialAsset->maskCutoff;

    shaderMaterial.metallicFactor = materialAsset->metallicFactor;
    shaderMaterial.roughnessFactor = materialAsset->roughnessFactor;

    shaderMaterial.colorTint = materialAsset->colorTint;

    MaterialHandle handle = m_managedMaterials.add(std::move(shaderMaterial));
    m_pendingMaterialUpdates.push_back(handle);

    return handle;
}

//MaterialHandle GpuScene::registerMaterial(MaterialAssetRaw* materialAsset)
//{
//    SCOPED_PROFILE_ZONE();
//    ASSERT_NOT_REACHED();
//}

void GpuScene::unregisterMaterial(MaterialHandle handle)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(m_managedMaterials.isValidHandle(handle));
    m_managedMaterials.removeReference(handle, m_currentFrameIdx);
}

TextureHandle GpuScene::registerMaterialTexture(std::optional<MaterialInput> const& input, ImageType imageType, Texture* fallback)
{
    SCOPED_PROFILE_ZONE();

    if (not input.has_value()) {
        TextureHandle handle = registerTextureSlot();
        updateTextureUnowned(handle, fallback);
        return handle;
    }

    std::string const& imageAssetPath = std::string(input->pathToImage());

    // TODO: Cache on material inputs, beyond just the path! If we don't handle this we can't update materials in runtime like we want as the path won't change.
    // Also, ensure that even if ref-count is zero it should be here in the map, and we remove from the deferred-delete list when we increment the count again
    auto entry = m_materialTextureCache.find(imageAssetPath);
    if (entry == m_materialTextureCache.end()) {

        auto makeTextureDescription = [](ImageAsset const& imageAsset, MaterialInput const& input, ImageType providedImageType) -> Texture::Description {
            // TODO: Handle 2D arrays & 3D textures here too
            ARKOSE_ASSERT(imageAsset.depth() == 1);

            ImageType imageType = imageAsset.type();
            ARKOSE_ASSERT(imageType == ImageType::Unknown || imageType == providedImageType);
            if (imageType == ImageType::Unknown) {
                imageType = providedImageType;
            }

            bool canGenerateMipmaps = not imageAsset.hasCompressedFormat();
            bool shouldUseMipmaps = input.useMipmapping && (imageAsset.numMips() > 1 || canGenerateMipmaps);

            return Texture::Description {
                .type = Texture::Type::Texture2D,
                .arrayCount = 1,
                .extent = { imageAsset.width(), imageAsset.height(), imageAsset.depth() },
                .format = Texture::convertImageFormatToTextureFormat(imageAsset.format(), imageType),
                .filter = Texture::Filters(Texture::convertImageFilterToMinFilter(input.minFilter),
                                           Texture::convertImageFilterToMagFilter(input.magFilter)),
                .wrapMode = input.wrapModes,
                .mipmap = Texture::convertImageFilterToMipFilter(input.mipFilter, shouldUseMipmaps),
                .multisampling = Texture::Multisampling::None
            };
        };

        TextureHandle handle = registerTextureSlot();
        m_materialTextureCache[imageAssetPath] = handle;

        // TODO: Also make the texture GPU resource itself on a worker thread, not just the image loading!
        if (UseAsyncTextureLoads) {

            // Put some placeholder texture for this texture slot before the async has loaded in fully
            updateTextureUnowned(handle, fallback);

            Task& task = Task::create([this, &makeTextureDescription, handle, imageType, path = imageAssetPath, input = *input]() {
                if (ImageAsset* imageAsset = ImageAsset::loadOrCreate(path)) {
                    Texture::Description desc = makeTextureDescription(*imageAsset, input, imageType);
                    {
                        SCOPED_PROFILE_ZONE_NAMED("Pushing async-loaded image asset");
                        std::scoped_lock<std::mutex> lock { m_asyncLoadedImagesMutex };
                        this->m_asyncLoadedImages.push_back(LoadedImageForTextureCreation { .imageAsset = imageAsset,
                                                                                            .textureHandle = handle,
                                                                                            .textureDescription = desc });
                    }
                }
            });

            task.autoReleaseOnCompletion();
            TaskGraph::get().scheduleTask(task);

        } else {
            if (ImageAsset* imageAsset = ImageAsset::loadOrCreate(imageAssetPath)) {
                std::unique_ptr<Texture> texture = m_backend.createTexture(makeTextureDescription(*imageAsset, *input, imageType));
                texture->setName("Texture<" + imageAssetPath + ">");

                ARKOSE_ASSERT(imageAsset->numMips() > 0);
                bool assetHasMips = imageAsset->numMips() > 1;
                bool textureWantMips = texture->mipmap() != Texture::Mipmap::None;

                if (not assetHasMips || not textureWantMips) {
                    texture->setData(imageAsset->pixelDataForMip(0).data(), imageAsset->pixelDataForMip(0).size(), 0);
                }

                if (textureWantMips) {
                    if (assetHasMips) {
                        for (size_t mipIdx = 0; mipIdx < imageAsset->numMips(); ++mipIdx) {
                            std::span<const u8> mipPixelData = imageAsset->pixelDataForMip(mipIdx);
                            texture->setData(mipPixelData.data(), mipPixelData.size(), mipIdx);
                        }
                    } else {
                        texture->generateMipmaps();
                    }
                }

                m_managedTexturesVramUsage += texture->sizeInMemory();
                updateTexture(handle, std::move(texture));
            } else {
                updateTextureUnowned(handle, fallback);
            }
        }

        return handle;
    }

    TextureHandle handle = entry->second;
    m_managedTextures.addReference(handle);

    return handle;
}

//TextureHandle GpuScene::registerMaterialTexture(MaterialInputRaw* input)
//{
//    SCOPED_PROFILE_ZONE();
//    ASSERT_NOT_REACHED();
//}

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
    TextureHandle handle = m_managedTextures.add(nullptr);
    return handle;
}

void GpuScene::updateTexture(TextureHandle handle, std::unique_ptr<Texture>&& texture)
{
    SCOPED_PROFILE_ZONE();

    auto const& setTexture = m_managedTextures.set(handle, std::move(texture));

    // TODO: What if the managed texture is deleted between now and the pending update? We need to protect against that!
    // One way would be to just put in the index in here and then when it's time to actually update, put in the texture pointer.

    // TODO: Pending texture updates should be unique for an index! Only use the latest texture for a given index! Even better,
    // why not just keep a single index to update here and we'll always use the managedTexture's texture for that index. The only
    // problem is that our current API doesn't know about managedTextures, so would need to convert to what the API accepts.

    m_pendingTextureUpdates.push_back({ .texture = setTexture.get(),
                                        .index = handle.indexOfType<uint32_t>() });
}

void GpuScene::updateTextureUnowned(TextureHandle handle, Texture* texture)
{
    ARKOSE_ASSERT(m_managedTextures.isValidHandle(handle));

    // TODO: If we have the same handle twice, probably remove/overwrite the first one! We don't want to send more updates than needed.
    // We could use a set (hashed on index) and always overwrite? Or eliminate duplicates at final step (see updateTexture comment above).

    auto index = handle.indexOfType<uint32_t>();
    m_pendingTextureUpdates.push_back({ .texture = texture,
                                        .index = index });
}

void GpuScene::unregisterTexture(TextureHandle handle)
{
    SCOPED_PROFILE_ZONE();

    if (m_managedTextures.removeReference(handle, m_currentFrameIdx)) {

        // If pending deletion, write symbolic blank texture to the index so nothing references the texture when time comes to remove it
        m_pendingTextureUpdates.push_back({ .texture = m_magentaTexture.get(),
                                            .index = handle.indexOfType<uint32_t>() });

    }
}

void GpuScene::processDeferredDeletions()
{
    // NOTE: In theory we can have this lower, but a higher value this will mean we keep a small window of time where the resources can be used again and thus not deleted.
    static constexpr size_t DeferredFrames = 10;
    static_assert(DeferredFrames >= 3, "To ensure correctness in all cases we must at least cover for tripple buffering");

    m_managedStaticMeshes.processDeferredDeletes(m_currentFrameIdx, DeferredFrames, [this](StaticMeshHandle handle, ManagedStaticMesh& managedStaticMesh) {
        // Unregister dependencies (materials)
        for (StaticMeshLOD const& lod : managedStaticMesh.staticMesh->LODs()) {
            for (StaticMeshSegment const& segment : lod.meshSegments) {
                unregisterMaterial(segment.material);
            }
        }

        if (UseMeshletRendering) {
            m_meshletManager->freeMeshlets(*managedStaticMesh.staticMesh);
        }

        managedStaticMesh.staticMeshAsset = nullptr;
        managedStaticMesh.staticMesh.reset();
    });

    m_managedMaterials.processDeferredDeletes(m_currentFrameIdx, DeferredFrames, [this](MaterialHandle handle, ShaderMaterial& shaderMaterial) {
        // Unregister dependencies (textures)
        unregisterTexture(TextureHandle(shaderMaterial.baseColor));
        unregisterTexture(TextureHandle(shaderMaterial.emissive));
        unregisterTexture(TextureHandle(shaderMaterial.normalMap));
        unregisterTexture(TextureHandle(shaderMaterial.metallicRoughness));

        shaderMaterial = ShaderMaterial();
        m_pendingMaterialUpdates.push_back(handle);
    });

    m_managedTextures.processDeferredDeletes(m_currentFrameIdx, DeferredFrames, [this](TextureHandle handle, std::unique_ptr<Texture>& texture) {
        // NOTE: Currently we can put null textures in the list if there is no texture, meaning we still reserve a texture slot and we have to handle that here.
        // TODO: Perhaps this isn't ideal? Consider if we can avoid reserving one altogether..
        if (texture != nullptr) {
            ARKOSE_ASSERT(m_managedTexturesVramUsage >= texture->sizeInMemory());
            m_managedTexturesVramUsage -= texture->sizeInMemory();

            // TODO: Intelligently remove from cache when we remove it from the resource list, don't just clear all!
            //m_materialTextureCache.erase ..
            m_materialTextureCache.clear();

            // Delete & clear from GPU memory immediately
            texture.reset();
        }
    });
}

DrawCallDescription GpuScene::fitVertexAndIndexDataForMesh(Badge<StaticMeshSegment>, const StaticMeshSegment& meshSegment, const VertexLayout& layout, std::optional<DrawCallDescription> alignWith)
{
    const size_t initialIndexBufferSize = 100'000 * sizeofIndexType(globalIndexBufferType());
    const size_t initialVertexBufferSize = 50'000 * layout.packedVertexSize();

    bool doAlign = alignWith.has_value();

    ARKOSE_ASSERT(meshSegment.asset);
    std::vector<u8> vertexData = meshSegment.asset->assembleVertexData(layout);

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

    uint32_t vertexCount = narrow_cast<u32>(meshSegment.asset->vertexCount());
    uint32_t vertexOffset = m_nextFreeVertexIndex;
    m_nextFreeVertexIndex += vertexCount;

    DrawCallDescription drawCall {};

    drawCall.vertexBuffer = &vertexBuffer;
    drawCall.vertexCount = vertexCount;
    drawCall.vertexOffset = vertexOffset;

    // Fit index data
    {
        std::vector<uint32_t> indexData = meshSegment.asset->indices;
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
    if (m_globalVertexBuffers.size() == 0) {
        return *m_emptyVertexBuffer;
    }

    auto entry = m_globalVertexBuffers.find(layout);
    if (entry == m_globalVertexBuffers.end()) {
        ARKOSE_LOG(Fatal, "Can't get vertex buffer for layout since it has not been created! Please ensureDrawCallIsAvailable for at least one mesh before calling this.");
    }

    return *entry->second;
}

Buffer& GpuScene::globalIndexBuffer() const
{
    if (m_global32BitIndexBuffer == nullptr) {
        return *m_emptyIndexBuffer;
    }

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
    ImGui::Text("static meshes: %u", m_managedStaticMeshes.size());
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
                heapNames.push_back(std::format("Heap{}", heapIdx));
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

            ImGui::Text("Number of BLASs: %d", m_totalNumBlas);

            float blasTotalSizeMB = conversion::to::MB(m_totalBlasVramUsage);
            ImGui::Text("BLAS total usage: %.2f MB", blasTotalSizeMB);

            float blasAverageSizeMB = blasTotalSizeMB / m_totalNumBlas;
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

