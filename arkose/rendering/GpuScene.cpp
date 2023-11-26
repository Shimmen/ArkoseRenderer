#include "GpuScene.h"

#include "asset/ImageAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/MeshAsset.h"
#include "asset/SkeletonAsset.h"
#include "rendering/Skeleton.h"
#include "rendering/DrawKey.h"
#include "rendering/RenderPipeline.h"
#include "rendering/backend/Resources.h"
#include "rendering/util/ScopedDebugZone.h"
#include "core/Logging.h"
#include "core/Types.h"
#include "core/parallel/TaskGraph.h"
#include "core/parallel/ParallelFor.h"
#include "rendering/Registry.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <ark/aabb.h>
#include <ark/conversion.h>
#include <ark/transform.h>
#include <concurrentqueue.h>
#include <fmt/format.h>
#include <unordered_set>

// Shared shader headers
#include "shaders/shared/CameraState.h"
#include "shaders/shared/ShaderBlendMode.h"

GpuScene::GpuScene(Scene& scene, Backend& backend, Extent2D initialMainViewportSize)
    : m_scene(scene)
    , m_backend(backend)
{
}

void GpuScene::initialize(Badge<Scene>, bool rayTracingCapable, bool meshShadingCapable)
{
    m_maintainRayTracingScene = rayTracingCapable;
    m_meshShadingCapable = meshShadingCapable;

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

    m_vertexManager = std::make_unique<VertexManager>(m_backend);

    if (m_maintainRayTracingScene) {
        m_sceneTopLevelAccelerationStructure = backend().createTopLevelAccelerationStructure(InitialMaxRayTracingGeometryInstanceCount, {});
    }

    if (m_meshShadingCapable) {
        m_meshletManager = std::make_unique<MeshletManager>(m_backend);
    }
}

void GpuScene::preRender()
{
}

void GpuScene::postRender()
{
    ParallelForBatched(m_staticMeshInstances.size(), 128, [this](size_t idx) {
        auto& instance = m_staticMeshInstances[idx];
        instance->transform().postRender({});
    });
}

SkeletalMesh* GpuScene::skeletalMeshForInstance(SkeletalMeshInstance const& instance)
{
    return skeletalMeshForHandle(instance.mesh());
}

SkeletalMesh const* GpuScene::skeletalMeshForInstance(SkeletalMeshInstance const& instance) const
{
    return skeletalMeshForHandle(instance.mesh());
}

SkeletalMesh* GpuScene::skeletalMeshForHandle(SkeletalMeshHandle handle)
{
    return handle.valid() ? m_managedSkeletalMeshes.get(handle).skeletalMesh.get() : nullptr;
}

SkeletalMesh const* GpuScene::skeletalMeshForHandle(SkeletalMeshHandle handle) const
{
    return handle.valid() ? m_managedSkeletalMeshes.get(handle).skeletalMesh.get() : nullptr;
}

StaticMesh* GpuScene::staticMeshForInstance(StaticMeshInstance const& staticMeshInstance)
{
    return staticMeshForHandle(staticMeshInstance.mesh());
}

StaticMesh const* GpuScene::staticMeshForInstance(StaticMeshInstance const& staticMeshInstance) const
{
    return staticMeshForHandle(staticMeshInstance.mesh());
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

ShaderDrawable const* GpuScene::drawableForHandle(DrawableObjectHandle handle) const
{
    return handle.valid() ? &m_drawables.get(handle) : nullptr;
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

void GpuScene::drawGui()
{
    ImGui::SliderFloat("Global mip bias", &m_globalMipBias, -10.0f, +10.0f);
}

RenderPipelineNode::ExecuteCallback GpuScene::construct(GpuScene&, Registry& reg)
{
    // TODO: For now, let's just always create the textures in the output display resolution,
    // and we use viewport command to draw within the viewport. Later we also want to save on
    // VRAM by creating smaller textures, but that makes it harder to easily change quality
    // or technique for upscaling, so this is the easier approach. It's also suitable for if
    // we want to implement dynamic resolution scaling in the future.
    Extent2D outputResolution = pipeline().outputResolution();
    Extent2D renderResolution = outputResolution;

    u32 numNodesAffectingRenderResolution = 0;
    for (RenderPipelineNode const* node : pipeline().nodes()) {
        if (node && node->isUpscalingNode()) {

            numNodesAffectingRenderResolution += 1;
            if (numNodesAffectingRenderResolution > 1) {
                ARKOSE_LOG(Error, "More than one nodes affects render resolution (e.g. does upscaling) so there's resolution ambiguity.");
                continue; // let's just listen to whatever the first node said
            }

            UpscalingTech tech = node->upscalingTech();
            UpscalingQuality quality = node->upscalingQuality();
            UpscalingPreferences preferences = backend().queryUpscalingPreferences(tech, quality, outputResolution);

            renderResolution = preferences.preferredRenderResolution;
            break;
        }
    }

    pipeline().setRenderResolution(renderResolution);
    camera().setViewport(renderResolution);

    // G-Buffer textures
    {
        auto nearestFilter = Texture::Filters::nearest();
        auto linearFilter = Texture::Filters::linear();
        auto mipMode = Texture::Mipmap::None;
        auto wrapMode = ImageWrapModes::clampAllToEdge();

        Texture& depthTexture = reg.createTexture2D(renderResolution, Texture::Format::Depth24Stencil8, nearestFilter, mipMode, wrapMode);
        reg.publish("SceneDepth", depthTexture);

        // rgb: scene color, a: unused
        Texture& colorTexture = reg.createTexture2D(renderResolution, Texture::Format::RGBA16F, linearFilter, mipMode, wrapMode);
        reg.publish("SceneColor", colorTexture);

        // rgb: scene diffuse irradiance, a: unused
        Texture& diffuseIrradianceTexture = reg.createTexture2D(renderResolution, Texture::Format::RGBA16F, linearFilter, mipMode, wrapMode);
        reg.publish("SceneDiffuseIrradiance", diffuseIrradianceTexture);

        // rg: encoded normal, ba: velocity in image plane (2D)
        Texture& normalVelocityTexture = reg.createTexture2D(renderResolution, Texture::Format::RGBA16F, linearFilter, mipMode, wrapMode);
        reg.publish("SceneNormalVelocity", normalVelocityTexture);

        // r: roughness, g: metallic, b: unused, a: unused
        Texture& materialTexture = reg.createTexture2D(renderResolution, Texture::Format::RGBA16F, linearFilter, mipMode, wrapMode);
        reg.publish("SceneMaterial", materialTexture);

        // rgb: base color, a: unused
        Texture& baseColorTexture = reg.createTexture2D(renderResolution, Texture::Format::RGBA8, linearFilter, mipMode, wrapMode);
        reg.publish("SceneBaseColor", baseColorTexture);

        // rgb: diffuse color, a: unused
        Texture& diffueGiTexture = reg.createTexture2D(renderResolution, Texture::Format::RGBA16F, linearFilter, mipMode, wrapMode);
        reg.publish("DiffuseGI", diffueGiTexture);
    }

    Buffer& cameraBuffer = reg.createBuffer(sizeof(CameraState), Buffer::Usage::ConstantBuffer, Buffer::MemoryHint::GpuOnly);
    BindingSet& cameraBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(cameraBuffer, ShaderStage::Any) });
    reg.publish("SceneCameraData", cameraBuffer);
    reg.publish("SceneCameraSet", cameraBindingSet);

    // Object data stuff
    // TODO: Resize the buffer if needed when more meshes are added, OR crash hard
    // TODO: Make a more reasonable default too... we need: #meshes * #LODs * #segments-per-lod
    size_t objectDataBufferSize = m_drawables.capacity() * sizeof(ShaderDrawable);
    //ARKOSE_LOG(Info, "Allocating space for {} instances, requiring {:.1f} MB of VRAM", m_drawables.capacity(), ark::conversion::to::MB(objectDataBufferSize));
    Buffer& objectDataBuffer = reg.createBuffer(objectDataBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("SceneObjectData", objectDataBuffer);
    BindingSet& objectBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(objectDataBuffer, ShaderStage::Vertex) });
    reg.publish("SceneObjectSet", objectBindingSet);


    // TODO: My lambda-system kind of fails horribly here. I need a reference-type for the capture to work nicely,
    //       and I also want to scope it under the if check. I either need to fix that or I'll need to make a pointer
    //       for it and then explicitly capture that pointer for this to work.
    Buffer* rtTriangleMeshBufferPtr = nullptr;
    if (m_maintainRayTracingScene) {
        // TODO: Resize the buffer if needed when more meshes are added, OR crash hard
        // TODO: Make a more reasonable default too... we need: #meshes * #LODs * #segments-per-lod
        Buffer& rtTriangleMeshBuffer = reg.createBuffer(10'000 * sizeof(RTTriangleMesh), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
        rtTriangleMeshBufferPtr = &rtTriangleMeshBuffer;

        BindingSet& rtMeshDataBindingSet = reg.createBindingSet({ ShaderBinding::storageBufferReadonly(rtTriangleMeshBuffer, ShaderStage::AnyRayTrace),
                                                                  ShaderBinding::storageBufferReadonly(vertexManager().indexBuffer(), ShaderStage::AnyRayTrace),
                                                                  ShaderBinding::storageBufferReadonly(vertexManager().nonPositionVertexBuffer(), ShaderStage::AnyRayTrace) });
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
                                                         ShaderBinding::storageBufferReadonly(dirLightDataBuffer),
                                                         ShaderBinding::storageBufferReadonly(sphereLightDataBuffer),
                                                         ShaderBinding::storageBufferReadonly(spotLightDataBuffer) });
    reg.publish("SceneLightSet", lightBindingSet);

    // Misc. data
    Texture& blueNoiseTextureArray = reg.loadTextureArrayFromFileSequence("assets/blue-noise/64_64/HDR_RGBA_{}.png", false, false);
    reg.publish("BlueNoise", blueNoiseTextureArray);

    // Skinning related
    m_jointMatricesBuffer = backend().createBuffer(1024 * sizeof(mat4), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    Shader skinningShader = Shader::createCompute("skinning/skinning.comp");
    BindingSet& skinningBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(m_vertexManager->positionVertexBuffer()),
                                                            ShaderBinding::storageBuffer(m_vertexManager->velocityDataVertexBuffer()),
                                                            ShaderBinding::storageBuffer(m_vertexManager->nonPositionVertexBuffer()),
                                                            ShaderBinding::storageBufferReadonly(m_vertexManager->skinningDataVertexBuffer()),
                                                            ShaderBinding::storageBufferReadonly(*m_jointMatricesBuffer) });
    ComputeState& skinningComputeState = reg.createComputeState(skinningShader, { &skinningBindingSet });

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
                                   ark::conversion::to::MB(sizeToUpload), ark::conversion::to::MB(textureUploadBudget));
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
        std::unordered_set<StaticMeshHandle> updatedStaticMeshes {};
        if (m_meshletManager != nullptr) {
            m_meshletManager->processMeshStreaming(cmdList, updatedStaticMeshes);
        }

        // Update camera data
        {
            Camera const& camera = this->camera();
            Extent2D renderResolution = pipeline().renderResolution();
            Extent2D outputResolution = pipeline().outputResolution();

            mat4 renderPixelFromView = camera.pixelProjectionMatrix(renderResolution.width(), renderResolution.height());
            //mat4 outputPixelFromView = camera.pixelProjectionMatrix(outputResolution.width(), outputResolution.height());

            mat4 projectionFromView = camera.projectionMatrix();
            mat4 viewFromWorld = camera.viewMatrix();

            CameraState cameraState {
                .projectionFromView = projectionFromView,
                .viewFromProjection = inverse(projectionFromView),
                .viewFromWorld = viewFromWorld,
                .worldFromView = inverse(viewFromWorld),

                .unjitteredProjectionFromView = camera.unjitteredProjectionMatrix(),

                .previousFrameProjectionFromView = camera.previousFrameProjectionMatrix(),
                .previousFrameViewFromWorld = camera.previousFrameViewMatrix(),

                .pixelFromView = renderPixelFromView,
                .viewFromPixel = inverse(renderPixelFromView),

                .frustumPlanes = { camera.frustum().plane(0).asVec4(),
                                   camera.frustum().plane(1).asVec4(),
                                   camera.frustum().plane(2).asVec4(),
                                   camera.frustum().plane(3).asVec4(),
                                   camera.frustum().plane(4).asVec4(),
                                   camera.frustum().plane(5).asVec4() },

                .renderResolution = vec4(renderResolution.asFloatVector(), renderResolution.inverse()),
                .outputResolution = vec4(outputResolution.asFloatVector(), outputResolution.inverse()),

                .zNear = camera.nearClipPlane(),
                .zFar = camera.farClipPlane(),

                .focalLength = camera.focalLengthMeters(),

                .iso = camera.ISO(),
                .aperture = camera.fNumber(),
                .shutterSpeed = camera.shutterSpeed(),
                .exposureCompensation = camera.exposureCompensation(),
            };

            uploadBuffer.upload(cameraState, cameraBuffer);
        }

        // Perform skinning for skeletal meshes
        if (m_skeletalMeshInstances.size() > 0) {
            SCOPED_PROFILE_ZONE_NAMED("Skinning");
            ScopedDebugZone skinningZone { cmdList, "Skinning" };

            cmdList.setComputeState(skinningComputeState);
            cmdList.bindSet(skinningBindingSet, 0);

            for (auto const& skeletalMeshInstance : m_skeletalMeshInstances) {

                std::vector<mat4> const& jointMatrices = skeletalMeshInstance->skeleton().appliedJointMatrices();
                // std::vector<mat3> const& jointTangentMatrices = skeletalMeshInstance->skeleton().appliedJointTangentMatrices();

                // TODO/OPTIMIZATION: Upload all instance's matrices in a single buffer once and simply offset into it!
                uploadBuffer.upload(jointMatrices, *m_jointMatricesBuffer);
                cmdList.executeBufferCopyOperations(uploadBuffer);

                for (SkinningVertexMapping const& skinningVertexMapping : skeletalMeshInstance->skinningVertexMappings()) {

                    ARKOSE_ASSERT(skinningVertexMapping.underlyingMesh.hasSkinningData());
                    ARKOSE_ASSERT(skinningVertexMapping.skinnedTarget.hasVelocityData());
                    ARKOSE_ASSERT(skinningVertexMapping.underlyingMesh.vertexCount == skinningVertexMapping.skinnedTarget.vertexCount);
                    u32 vertexCount = skinningVertexMapping.underlyingMesh.vertexCount;

                    cmdList.setNamedUniform<u32>("firstSrcVertexIdx", skinningVertexMapping.underlyingMesh.firstVertex);
                    cmdList.setNamedUniform<u32>("firstDstVertexIdx", skinningVertexMapping.skinnedTarget.firstVertex);
                    cmdList.setNamedUniform<u32>("firstSkinningVertexIdx", static_cast<u32>(skinningVertexMapping.underlyingMesh.firstSkinningVertex));
                    cmdList.setNamedUniform<u32>("firstVelocityVertexIdx", static_cast<u32>(skinningVertexMapping.skinnedTarget.firstVelocityVertex));
                    cmdList.setNamedUniform<u32>("vertexCount", skinningVertexMapping.underlyingMesh.vertexCount);

                    constexpr u32 localSize = 32;
                    cmdList.dispatch({ vertexCount, 1, 1 }, { localSize, 1, 1 });
                }

                if (m_maintainRayTracingScene) {

                    // TODO/OPTIMIZATION: We can do away with just one of these barriers if we process all skeletal mesh instances as one (see above)
                    cmdList.bufferWriteBarrier({ &vertexManager().positionVertexBuffer(), &vertexManager().nonPositionVertexBuffer() });

                    for (auto& blas : skeletalMeshInstance->BLASes()) {
                        cmdList.buildBottomLevelAcceratationStructure(*blas, AccelerationStructureBuildType::Update);
                    }
                }
            }

            cmdList.bufferWriteBarrier({ &vertexManager().positionVertexBuffer(),
                                         &vertexManager().nonPositionVertexBuffer(),
                                         &vertexManager().velocityDataVertexBuffer() });
        }

        // Update object data (drawables)
        {
            moodycamel::ConcurrentQueue<StaticMeshInstance*> instancesNeedingReinit {};

            std::atomic<size_t> drawableCount { 0 };
            ParallelForBatched(staticMeshInstances().size(), 64, [this, &drawableCount, &updatedStaticMeshes, &instancesNeedingReinit](size_t idx) {
                auto& instance = staticMeshInstances()[idx];

                bool meshHasUpdated = updatedStaticMeshes.contains(instance->mesh());

                if (meshHasUpdated) {
                    // Full update: reinit the mesh instance
                    instancesNeedingReinit.enqueue(instance.get());
                } else {
                    // Minimal update: only change transforms

                    // Consider moving transforms to a per mesh-instance basis and let the drawables only keep a mesh-instance index and a material-index.
                    // This would mean another indirection on the GPU when looking up transforms, but significantly less updating and iterating on the CPU
                    // to e.g. update transforms.
                    for (DrawableObjectHandle drawableHandle : instance->drawableHandles()) {
                        ShaderDrawable& drawable = m_drawables.get(drawableHandle);
                        drawable.worldFromLocal = instance->transform().worldMatrix();
                        drawable.worldFromTangent = mat4(instance->transform().worldNormalMatrix());
                        drawable.previousFrameWorldFromLocal = instance->transform().previousFrameWorldMatrix();
                    }
                }

                drawableCount += instance->drawableHandles().size();
            });

            // NOTE: `try_dequeue` should be able to empty the entire queue as all producers are done at this point in time
            StaticMeshInstance* instanceNeedingReinit;
            while (instancesNeedingReinit.try_dequeue(instanceNeedingReinit)) {
                initializeStaticMeshInstance(*instanceNeedingReinit);
            }

            for (auto& skeletalMeshInstance : m_skeletalMeshInstances) {

                for (DrawableObjectHandle drawableHandle : skeletalMeshInstance->drawableHandles()) {
                    ShaderDrawable& drawable = m_drawables.get(drawableHandle);
                    drawable.worldFromLocal = skeletalMeshInstance->transform().worldMatrix();
                    drawable.worldFromTangent = mat4(skeletalMeshInstance->transform().worldNormalMatrix());
                    drawable.previousFrameWorldFromLocal = skeletalMeshInstance->transform().previousFrameWorldMatrix();
                }

                drawableCount += skeletalMeshInstance->drawableHandles().size();
            }

            m_drawableCountForFrame = drawableCount;
            uploadBuffer.upload(m_drawables.resourceSpan(), objectDataBuffer);
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

            SCOPED_PROFILE_ZONE_NAMED("Update TLAS");

            auto tlasBuildType = AccelerationStructureBuildType::Update;

            // TODO: Fill in both of these and upload to the GPU buffers. For now they will be 1:1
            std::vector<RTTriangleMesh> rayTracingMeshData {};
            std::vector<RTGeometryInstance> rayTracingGeometryInstances {};

            for (auto& instance : staticMeshInstances()) {
                if (StaticMesh* staticMesh = staticMeshForHandle(instance->mesh())) {
                    for (StaticMeshLOD& staticMeshLOD : staticMesh->LODs()) {
                        for (StaticMeshSegment& meshSegment : staticMeshLOD.meshSegments) {

                            u32 rtMeshIndex = narrow_cast<u32>(rayTracingMeshData.size());

                            DrawCallDescription drawCallDesc = meshSegment.vertexAllocation.asDrawCallDescription();
                            rayTracingMeshData.push_back(RTTriangleMesh { .firstVertex = drawCallDesc.vertexOffset,
                                                                          .firstIndex = static_cast<int>(drawCallDesc.firstIndex),
                                                                          .materialIndex = meshSegment.material.indexOfType<int>() });

                            ARKOSE_ASSERT(meshSegment.blas != nullptr);
                            tlasBuildType = AccelerationStructureBuildType::FullBuild; // TODO: Only do a full rebuild sometimes!
                            /*
                            if (meshSegment.blas == nullptr) {
                                meshSegment.blas = createBottomLevelAccelerationStructure(meshSegment);

                                m_totalNumBlas += 1;
                                m_totalBlasVramUsage += meshSegment.blas->sizeInMemory();

                                // We have new instances, a full build is needed
                                tlasBuildType = AccelerationStructureBuildType::FullBuild;
                            }
                            */

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
                            rayTracingGeometryInstances.push_back(RTGeometryInstance { .blas = meshSegment.blas.get(),
                                                                                       .transform = &instance->transform(),
                                                                                       .shaderBindingTableOffset = 0, // TODO: Generalize!
                                                                                       .customInstanceId = rtMeshIndex,
                                                                                       .hitMask = hitMask });
                        }
                    }
                }
            }

            for (auto& instance : skeletalMeshInstances()) {
                if (SkeletalMesh* skeletalMesh = skeletalMeshForHandle(instance->mesh())) {
                    StaticMesh const& staticMesh = skeletalMesh->underlyingMesh();
                    for (StaticMeshLOD const& staticMeshLOD : staticMesh.LODs()) {
                        for (u32 segmentIdx = 0; segmentIdx < staticMeshLOD.meshSegments.size(); ++segmentIdx) {
                            StaticMeshSegment const& meshSegment = staticMeshLOD.meshSegments[segmentIdx];

                            u32 rtMeshIndex = narrow_cast<u32>(rayTracingMeshData.size());

                            DrawCallDescription drawCallDesc = meshSegment.vertexAllocation.asDrawCallDescription();
                            rayTracingMeshData.push_back(RTTriangleMesh { .firstVertex = drawCallDesc.vertexOffset,
                                                                            .firstIndex = static_cast<int>(drawCallDesc.firstIndex),
                                                                            .materialIndex = meshSegment.material.indexOfType<int>() });

                            BottomLevelAS const& blas = *instance->blasForSegmentIndex(segmentIdx);

                            tlasBuildType = AccelerationStructureBuildType::FullBuild; // TODO: Only do a full rebuild sometimes!

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
                            rayTracingGeometryInstances.push_back(RTGeometryInstance { .blas = &blas,
                                                                                       .transform = &instance->transform(),
                                                                                       .shaderBindingTableOffset = 0, // TODO: Generalize!
                                                                                       .customInstanceId = rtMeshIndex,
                                                                                       .hitMask = hitMask });
                        }
                    }
                }

                // TODO: Ensure there is a BLAS, update it, and make an instance of it for the TLAS
                // NOTE: We don't need to dig into the skeletal mesh underneath since the instance has its own
                //       buffers which the skinning manager should keep up to date every frame!
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

void GpuScene::clearAllMeshInstances()
{
    for (auto& instance : m_staticMeshInstances) {
        unregisterStaticMesh(instance->mesh());
    }

    for (auto& instance : m_skeletalMeshInstances) {
        unregisterSkeletalMesh(instance->mesh());
    }

    m_staticMeshInstances.clear();
    m_skeletalMeshInstances.clear();
}

SkeletalMeshInstance& GpuScene::createSkeletalMeshInstance(SkeletalMeshHandle skeletalMeshHandle, Transform transform)
{
    // TODO: Do we not need to add a reference here? Yes, but we'd over count by one as the managed mesh itself has the first ref.
    // m_managedSkeletalMeshes.addReference(skeletalMeshHandle);

    ManagedSkeletalMesh& managedSkeletalMesh = m_managedSkeletalMeshes.get(skeletalMeshHandle);
    auto skeleton = std::make_unique<Skeleton>(managedSkeletalMesh.skeletonAsset);

    m_skeletalMeshInstances.push_back(std::make_unique<SkeletalMeshInstance>(skeletalMeshHandle, std::move(skeleton), transform));
    SkeletalMeshInstance& instance = *m_skeletalMeshInstances.back();

    initializeSkeletalMeshInstance(instance);

    return instance;
}

void GpuScene::initializeSkeletalMeshInstance(SkeletalMeshInstance& instance)
{
    SkeletalMesh* skeletalMesh = skeletalMeshForHandle(instance.mesh());
    ARKOSE_ASSERT(skeletalMesh != nullptr);

    StaticMesh& underlyingMesh = skeletalMesh->underlyingMesh();

    constexpr u32 lodIdx = 0;
    StaticMeshLOD& lod = underlyingMesh.lodAtIndex(lodIdx);

    // TODO: Handle LOD changes for this instance! If it changes we want to unregister our current ones and register the ones for the new LOD
    // instance.resetDrawableHandles();
    // instance.resetSkinningVertexMappings();

    for (size_t segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
        StaticMeshSegment& meshSegment = lod.meshSegments[segmentIdx];

        ShaderDrawable drawable;
        drawable.worldFromLocal = instance.transform().worldMatrix();
        drawable.worldFromTangent = mat4(instance.transform().worldNormalMatrix());
        drawable.previousFrameWorldFromLocal = instance.transform().previousFrameWorldMatrix();

        drawable.localBoundingSphere = underlyingMesh.boundingSphere().asVec4();

        drawable.materialIndex = meshSegment.material.indexOfType<int>();

        DrawKey drawKey = meshSegment.drawKey;
        ARKOSE_ASSERT(drawKey.hasExplicityVelocity() == false);
        drawKey.setHasExplicityVelocity(true);
        drawable.drawKey = drawKey.asUint32();

        // For now, don't use meshlets for skeletal meshes as we don't know how to map
        // the animated vertices to the meshlets vertices easily. It's solvable, but not for now.
        drawable.firstMeshlet = 0;
        drawable.meshletCount = 0;

        if (instance.hasDrawableHandleForSegmentIndex(segmentIdx)) {
            DrawableObjectHandle handle = instance.drawableHandleForSegmentIndex(segmentIdx);
            m_drawables.set(handle, std::move(drawable));
        } else {
            DrawableObjectHandle handle = m_drawables.add(std::move(drawable));
            instance.setDrawableHandle(segmentIdx, handle);
        }

        if (!instance.hasSkinningVertexMappingForSegmentIndex(segmentIdx)) {
            // We don't need to allocate indices or skinning for the target. The indices will duplicate the underlying mesh
            // as it's never changed, and skinning data will never be needed for the *target*. We do have to allocate space
            // for velocity data, however, as it's something that's specific for the animated target vertices.
            constexpr bool includeIndices = false;
            constexpr bool includeSkinningData = false;
            constexpr bool includeVelocityData = true;

            VertexAllocation instanceVertexAllocation = m_vertexManager->allocateMeshDataForSegment(*meshSegment.asset,
                                                                                                    includeIndices,
                                                                                                    includeSkinningData,
                                                                                                    includeVelocityData);
            ARKOSE_ASSERT(instanceVertexAllocation.isValid());

            instanceVertexAllocation.firstIndex = meshSegment.vertexAllocation.firstIndex;
            instanceVertexAllocation.indexCount = meshSegment.vertexAllocation.indexCount;

            SkinningVertexMapping skinningVertexMapping { .underlyingMesh = meshSegment.vertexAllocation,
                                                          .skinnedTarget = instanceVertexAllocation };
            instance.setSkinningVertexMapping(segmentIdx, skinningVertexMapping);
        }

        if (m_maintainRayTracingScene) {
            // For now at least, this is not reentrant.
            //  ... but we can have more than one segment instance per mesh..
            //ARKOSE_ASSERT(instance.BLASes().size() == 0);

            SkinningVertexMapping const& skinningVertexMappings = instance.skinningVertexMappingForSegmentIndex(segmentIdx);
            ARKOSE_ASSERT(skinningVertexMappings.skinnedTarget.isValid());

            // NOTE: We construct the new BLAS into its own buffers but 1) we don't have any data in there yet to build from,
            // and 2) we don't want to build redundantly, so we pass in the existing BLAS from the underlying mesh as a BLAS
            // copy source, which means that we copy the built BLAS into place.
            BottomLevelAS const* sourceBlas = meshSegment.blas.get();

            auto blas = m_vertexManager->createBottomLevelAccelerationStructure(skinningVertexMappings.skinnedTarget, sourceBlas);
            instance.setBLAS(segmentIdx, std::move(blas));
        }
    }
}

StaticMeshInstance& GpuScene::createStaticMeshInstance(StaticMeshHandle staticMeshHandle, Transform transform)
{
    // TODO: Do we not need to add a reference here? I would think yes, but it seems to already be accounted for?
    //m_managedStaticMeshes.addReference(staticMeshHandle);

    m_staticMeshInstances.push_back(std::make_unique<StaticMeshInstance>(staticMeshHandle, transform));
    StaticMeshInstance& instance = *m_staticMeshInstances.back();

    initializeStaticMeshInstance(instance);

    return instance;
}

void GpuScene::initializeStaticMeshInstance(StaticMeshInstance& instance)
{
    StaticMesh* staticMesh = staticMeshForHandle(instance.mesh());
    ARKOSE_ASSERT(staticMesh != nullptr);

    constexpr u32 lodIdx = 0;
    StaticMeshLOD& lod = staticMesh->lodAtIndex(lodIdx);

    // TODO: Handle LOD changes for this instance! If it changes we want to unregister our current ones and register the ones for the new LOD
    //instance.resetDrawableHandles();

    for (size_t segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
        StaticMeshSegment& meshSegment = lod.meshSegments[segmentIdx];

        ShaderDrawable drawable;
        drawable.worldFromLocal = instance.transform().worldMatrix();
        drawable.worldFromTangent = mat4(instance.transform().worldNormalMatrix());
        drawable.previousFrameWorldFromLocal = instance.transform().previousFrameWorldMatrix();

        drawable.localBoundingSphere = staticMesh->boundingSphere().asVec4();

        drawable.materialIndex = meshSegment.material.indexOfType<int>();

        drawable.drawKey = meshSegment.drawKey.asUint32();

        if (meshSegment.meshletView) {
            drawable.firstMeshlet = meshSegment.meshletView->firstMeshlet;
            drawable.meshletCount = meshSegment.meshletView->meshletCount;
        } else {
            drawable.firstMeshlet = 0;
            drawable.meshletCount = 0;
        }

        if (instance.hasDrawableHandleForSegmentIndex(segmentIdx)) {
            DrawableObjectHandle handle = instance.drawableHandleForSegmentIndex(segmentIdx);
            m_drawables.set(handle, std::move(drawable));
        } else {
            DrawableObjectHandle handle = m_drawables.add(std::move(drawable));
            instance.setDrawableHandle(segmentIdx, handle);
        }
    }
}

SkeletalMeshHandle GpuScene::registerSkeletalMesh(MeshAsset const* meshAsset, SkeletonAsset const* skeletonAsset)
{
    SCOPED_PROFILE_ZONE();

    if (meshAsset == nullptr || skeletonAsset == nullptr) {
        return SkeletalMeshHandle();
    }

    // TODO: Maybe do some kind of caching here, similar to how we do it for static meshes?
    //  Also, if this skeletal mesh has been registered as a static mesh it should also be valid..?

    auto skeletalMesh = std::make_unique<SkeletalMesh>(meshAsset, skeletonAsset, [this](MaterialAsset const* materialAsset) -> MaterialHandle {
        if (materialAsset) {
            return registerMaterial(materialAsset);
        } else {
            return m_defaultMaterialHandle;
        }
    });

    if (m_vertexManager != nullptr) {
        constexpr bool includeIndices = true;
        constexpr bool includeSkinningData = true;
        m_vertexManager->uploadMeshData(skeletalMesh->underlyingMesh(), includeIndices, includeSkinningData);

        if (m_maintainRayTracingScene) {
            // NOTE: This isn't the most ideal memory usage as we're having this BLAS just sitting here and just being
            // used as a copy source, but it makes things easy now. Later we can make it when the first instance is
            // created and then let that first instance be the copy source for all other instances.
            m_vertexManager->createBottomLevelAccelerationStructure(skeletalMesh->underlyingMesh());
        }
    }

    SkeletalMeshHandle handle = m_managedSkeletalMeshes.add(ManagedSkeletalMesh { .meshAsset = meshAsset,
                                                                                  .skeletonAsset = skeletonAsset,
                                                                                  .skeletalMesh = std::move(skeletalMesh) });

    // The skeletal mesh will in some cases want a handle back to itself
    // NOTE: Needed for our meshlet streaming system. For now though we just
    // reinit the skeletal instances every frame so we don't need to track this.
    //m_managedSkeletalMeshes.get(handle).skeletalMesh->setHandleToSelf(handle);

    return handle;
}

void GpuScene::unregisterSkeletalMesh(SkeletalMeshHandle handle)
{
    // Do we really want to reference count this..? See GpuScene::unregisterStaticMesh(..)
    m_managedSkeletalMeshes.removeReference(handle, m_currentFrameIdx);
}

StaticMeshHandle GpuScene::registerStaticMesh(MeshAsset const* meshAsset)
{
    // TODO: Maybe do some kind of caching here, and if we're trying to add the same mesh twice just ignore it and reuse the exisiting

    SCOPED_PROFILE_ZONE();

    if (meshAsset == nullptr) {
        return StaticMeshHandle();
    }

    auto entry = m_staticMeshAssetCache.find(meshAsset);
    if (entry != m_staticMeshAssetCache.end()) {
        return entry->second;
    }

    auto staticMesh = std::make_unique<StaticMesh>(meshAsset, [this](MaterialAsset const* materialAsset) -> MaterialHandle {
        if (materialAsset) {
            return registerMaterial(materialAsset);
        } else {
            return m_defaultMaterialHandle;
        }
    });

    if (m_vertexManager != nullptr) {
        constexpr bool includeIndices = true;
        constexpr bool includeSkinningData = false;
        m_vertexManager->uploadMeshData(*staticMesh, includeIndices, includeSkinningData);

        if (m_maintainRayTracingScene) {
            m_vertexManager->createBottomLevelAccelerationStructure(*staticMesh);
        }
    }

    if (m_meshletManager != nullptr) {
        m_meshletManager->allocateMeshlets(*staticMesh);
    }

    StaticMeshHandle handle = m_managedStaticMeshes.add(ManagedStaticMesh { .meshAsset = meshAsset,
                                                                            .staticMesh = std::move(staticMesh) });

    // The static mesh will in some cases want a handle back to itself
    m_managedStaticMeshes.get(handle).staticMesh->setHandleToSelf(handle);

    m_staticMeshAssetCache[meshAsset] = handle;

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

        if (m_meshletManager != nullptr) {
            m_meshletManager->freeMeshlets(*managedStaticMesh.staticMesh);
        }

        managedStaticMesh.meshAsset = nullptr;
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

VertexManager const& GpuScene::vertexManager() const
{
    ARKOSE_ASSERT(m_vertexManager != nullptr);
    return *m_vertexManager;
}

MeshletManager const& GpuScene::meshletManager() const
{
    ARKOSE_ASSERT(m_meshletManager != nullptr);
    return *m_meshletManager;
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

        float currentTotalUsedGB = ark::conversion::to::GB(stats.totalUsed);
        ImGui::Text("Current VRAM usage: %.2f GB", currentTotalUsedGB);

        for (size_t heapIdx = 0, heapCount = stats.heaps.size(); heapIdx < heapCount; ++heapIdx) {
            if (heapIdx >= m_vramUsageHistoryPerHeap.size()) {
                m_vramUsageHistoryPerHeap.resize(heapIdx + 1);
            }
            if (ImGui::GetFrameCount() % backend().vramStatsReportRate() == 0) {
                float heapUsedMB = ark::conversion::to::MB(stats.heaps[heapIdx].used);
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
                float heapUsedMB = ark::conversion::to::MB(heap.used);
                float heapAvailableMB = ark::conversion::to::MB(heap.available);
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
                    float heapAvailableMB = ark::conversion::to::MB(stats.heaps[i].available);
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

            ImGui::Text("Number of managed textures: %ul", m_managedTextures.size());

            float managedTexturesTotalGB = ark::conversion::to::GB(m_managedTexturesVramUsage);
            ImGui::Text("Using %.2f GB", managedTexturesTotalGB);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Mesh index data")) {

            ImGui::Text("Using global index type %s", indexTypeToString(vertexManager().indexType()));

            // TODO: Port over to the vertex manager!
            //float allocatedSizeMB = conversion::to::MB(indexBuffer().sizeInMemory());
            //float usedSizeMB = conversion::to::MB(m_nextFreeIndex * sizeofIndexType(vertexManager().indexType()));
            //ImGui::Text("Using %.1f MB (%.1f MB allocated)", usedSizeMB, allocatedSizeMB);

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

                // TODO: Port over to the vertex manager!
                /*
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
                */

                ImGui::EndTable();
            }

            ImGui::Separator();
            ImGui::Text("Total: %.2f MB (%.2f MB allocated)", totalUsedSizeMB, totalAllocatedSizeMB);

            ImGui::EndTabItem();
        }

        /*
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
        */

        ImGui::EndTabBar();
    }

    if (includeContainingWindow) {
        ImGui::End();
    }
}

