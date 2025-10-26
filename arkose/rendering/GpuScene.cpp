#include "GpuScene.h"

#include "asset/ImageAsset.h"
#include "asset/MaterialAsset.h"
#include "asset/MeshAsset.h"
#include "asset/SkeletonAsset.h"
#include "asset/external/CubeLUT.h"
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
#include <ark/color.h>
#include <ark/conversion.h>
#include <ark/transform.h>
#include <concurrentqueue.h>
#include <fmt/format.h>
#include <unordered_set>

// Shared shader headers
#include "shaders/shared/CameraState.h"
#include "shaders/shared/ShaderBlendMode.h"

GpuScene::GpuScene(Scene& scene, Backend& backend)
    : m_scene(scene)
    , m_backend(backend)
{
#if defined(PLATFORM_WINDOWS)
    natvis_managedStaticMeshes = &m_managedStaticMeshes;
    natvis_managedTextures = &m_managedTextures;
#endif // defined(PLATFORM_WINDOWS)
}

void GpuScene::initialize(Badge<Scene>, bool rayTracingCapable, bool meshShadingCapable)
{
    SCOPED_PROFILE_ZONE();

    m_maintainRayTracingScene = rayTracingCapable;
    m_meshShadingCapable = meshShadingCapable;

    m_emptyVertexBuffer = backend().createBuffer(1, Buffer::Usage::Vertex);
    m_emptyIndexBuffer = backend().createBuffer(1, Buffer::Usage::Index);

    m_blackTexture = Texture::createFromPixel(backend(), vec4(0.0f, 0.0f, 0.0f, 0.0f), true);
    m_whiteTexture = Texture::createFromPixel(backend(), vec4(1.0f, 1.0f, 1.0f, 1.0f), true);
    m_lightGrayTexture = Texture::createFromPixel(backend(), vec4(0.75f, 0.75f, 0.75f, 1.0f), true);
    m_magentaTexture = Texture::createFromPixel(backend(), vec4(1.0f, 0.0f, 1.0f, 1.0f), true);
    m_normalMapBlueTexture = Texture::createFromPixel(backend(), vec4(0.5f, 0.5f, 1.0f, 1.0f), false);

    // Create default samplers
    {
        Sampler::Description samplerDescNearest { .minFilter = ImageFilter::Nearest, .magFilter = ImageFilter::Nearest, .mipmap = Sampler::Mipmap::Nearest };
        Sampler::Description samplerDescBilinear { .minFilter = ImageFilter::Linear, .magFilter = ImageFilter::Linear, .mipmap = Sampler::Mipmap::Nearest };
        Sampler::Description samplerDescTrilinear { .minFilter = ImageFilter::Linear, .magFilter = ImageFilter::Linear, .mipmap = Sampler::Mipmap::Linear };

        samplerDescNearest.wrapMode = ImageWrapModes::clampAllToEdge();
        samplerDescBilinear.wrapMode = ImageWrapModes::clampAllToEdge();
        samplerDescTrilinear.wrapMode = ImageWrapModes::clampAllToEdge();
        m_samplerClampNearest = backend().createSampler(samplerDescNearest);
        m_samplerClampBilinear = backend().createSampler(samplerDescBilinear);
        m_samplerClampTrilinear = backend().createSampler(samplerDescTrilinear);

        samplerDescNearest.wrapMode = ImageWrapModes::repeatAll();
        samplerDescBilinear.wrapMode = ImageWrapModes::repeatAll();
        samplerDescTrilinear.wrapMode = ImageWrapModes::repeatAll();
        m_samplerRepeatNearest = backend().createSampler(samplerDescNearest);
        m_samplerRepeatBilinear = backend().createSampler(samplerDescBilinear);
        m_samplerRepeatTrilinear = backend().createSampler(samplerDescTrilinear);

        samplerDescNearest.wrapMode = ImageWrapModes::mirroredRepeatAll();
        samplerDescBilinear.wrapMode = ImageWrapModes::mirroredRepeatAll();
        samplerDescTrilinear.wrapMode = ImageWrapModes::mirroredRepeatAll();
        m_samplerMirrorNearest = backend().createSampler(samplerDescNearest);
        m_samplerMirrorBilinear = backend().createSampler(samplerDescBilinear);
        m_samplerMirrorTrilinear = backend().createSampler(samplerDescTrilinear);
    }

    m_iconManager = std::make_unique<IconManager>(backend());

    size_t materialBufferSize = m_managedMaterials.capacity() * sizeof(ShaderMaterial);
    m_materialDataBuffer = backend().createBuffer(materialBufferSize, Buffer::Usage::StorageBuffer);
    m_materialDataBuffer->setStride(sizeof(ShaderMaterial));
    m_materialDataBuffer->setName("SceneMaterialData");

    MaterialAsset defaultMaterialAsset {};
    defaultMaterialAsset.colorTint = vec4(1.0f, 0.0f, 1.0f, 1.0f);
    m_defaultMaterialHandle = registerMaterial(&defaultMaterialAsset);

    // TODO: Get rid of this placeholder that we use to write into all texture slots (i.e. support partially bound etc.)
    std::vector<Texture*> placeholderTexture = { m_magentaTexture.get() };
    m_materialBindingSet = backend().createBindingSet({ ShaderBinding::storageBuffer(*m_materialDataBuffer.get()),
                                                        ShaderBinding::sampledTextureBindlessArray(static_cast<uint32_t>(m_managedTextures.capacity()), placeholderTexture) });
    m_materialBindingSet->setName("SceneMaterialSet");

    // TODO: Set up from somewhere more logical/opinionated source, like the scene/level?
    auto identityLUT = CubeLUT::load("assets/engine/lut/identity.cube");
    updateColorGradingLUT(*identityLUT);

    m_vertexManager = std::make_unique<VertexManager>(m_backend, *this);

    if (m_maintainRayTracingScene) {
        m_sceneTopLevelAccelerationStructure = backend().createTopLevelAccelerationStructure(InitialMaxRayTracingGeometryInstanceCount);
    }
}

void GpuScene::preRender()
{
}

void GpuScene::postRender()
{
    ParallelForBatched(m_staticMeshInstances.size(), 256, [this](size_t idx) {
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

Texture const* GpuScene::textureForHandle(TextureHandle handle) const
{
    return handle.valid() ? m_managedTextures.get(handle).get() : nullptr;
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

size_t GpuScene::forEachLocalRTShadow(std::function<void(size_t, Light const&, Texture& shadowMask)> callback) const
{
    size_t nextIndex = 0;
    for (auto& managedLight : m_managedSpotLights) {
        if (managedLight.light->shadowMode() == ShadowMode::RayTraced && managedLight.shadowMaskTexture) {
            callback(nextIndex++, *managedLight.light, *managedLight.shadowMaskTexture);
        }
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
    Extent2D const& outputResolution = reg.allocate<Extent2D>(pipeline().outputResolution());
    Extent2D& renderResolution = reg.allocate<Extent2D>(outputResolution);

    u32 numNodesAffectingRenderResolution = 0;
    for (RenderPipelineNode const* node : pipeline().nodes()) {
        if (node && node->isUpscalingNode()) {

            numNodesAffectingRenderResolution += 1;
            if (numNodesAffectingRenderResolution > 1) {
                ARKOSE_LOG(Error, "More than one nodes affects render resolution (e.g. does upscaling) so there's resolution ambiguity.");
                continue; // let's just listen to whatever the first node said
            }

            renderResolution = node->idealRenderResolution(outputResolution);
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

        // rgb: bent normal, a: bent cone
        Texture& bentNormalTexture = reg.createTexture2D(renderResolution, Texture::Format::RGBA16F, linearFilter, mipMode, wrapMode);
        reg.publish("SceneBentNormal", bentNormalTexture);

        // r: roughness, g: metallic, b: occlusion, a: unused
        Texture& materialTexture = reg.createTexture2D(renderResolution, Texture::Format::RGBA8, linearFilter, mipMode, wrapMode);
        reg.publish("SceneMaterial", materialTexture);

        // rgb: base color, a: unused
        Texture& baseColorTexture = reg.createTexture2D(renderResolution, Texture::Format::RGBA8, linearFilter, mipMode, wrapMode);
        reg.publish("SceneBaseColor", baseColorTexture);

        // rgb: diffuse color, a: unused
        Texture& diffueGiTexture = reg.createTexture2D(renderResolution, Texture::Format::RGBA16F, linearFilter, mipMode, wrapMode);
        reg.publish("DiffuseGI", diffueGiTexture);
    }

    Buffer& cameraBuffer = reg.createBuffer(sizeof(CameraState), Buffer::Usage::ConstantBuffer);
    BindingSet& cameraBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(cameraBuffer, ShaderStage::Any) });
    reg.publish("SceneCameraData", cameraBuffer);
    reg.publish("SceneCameraSet", cameraBindingSet);

    // Object data stuff
    // TODO: Resize the buffer if needed when more meshes are added, OR crash hard
    // TODO: Make a more reasonable default too... we need: #meshes * #LODs * #segments-per-lod
    size_t objectDataBufferSize = m_drawables.capacity() * sizeof(ShaderDrawable);
    //ARKOSE_LOG(Info, "Allocating space for {} instances, requiring {:.1f} MB of VRAM", m_drawables.capacity(), ark::conversion::to::MB(objectDataBufferSize));
    Buffer& objectDataBuffer = reg.createBuffer(objectDataBufferSize, Buffer::Usage::StorageBuffer);
    objectDataBuffer.setStride(sizeof(ShaderDrawable));
    reg.publish("SceneObjectData", objectDataBuffer);
    BindingSet& objectBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(objectDataBuffer, ShaderStage::Vertex) });
    reg.publish("SceneObjectSet", objectBindingSet);

    // Visibility buffer textures & data
    if (m_meshShadingCapable) {
        Texture::Description visibilityDataTexDesc { .extent = pipeline().renderResolution(),
                                                     .format = Texture::Format::R32Uint };

        Texture& instanceVisibilityTexture = reg.createTexture(visibilityDataTexDesc);
        reg.publish("InstanceVisibilityTexture", instanceVisibilityTexture);

        Texture& triangleVisibilityTexture = reg.createTexture(visibilityDataTexDesc);
        reg.publish("TriangleVisibilityTexture", triangleVisibilityTexture);

        // Binding set for all data required to interpret the visibility buffer - just get this binding set when you need to read it!
        BindingSet& visBufferDataBindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(instanceVisibilityTexture),
                                                                     ShaderBinding::sampledTexture(triangleVisibilityTexture),
                                                                     ShaderBinding::storageBufferReadonly(*reg.getBuffer("SceneObjectData")),
                                                                     ShaderBinding::storageBufferReadonly(vertexManager().meshletBuffer()),
                                                                     ShaderBinding::storageBufferReadonly(vertexManager().meshletIndexBuffer()),
                                                                     ShaderBinding::storageBufferReadonly(vertexManager().meshletVertexIndirectionBuffer()),
                                                                     ShaderBinding::storageBufferReadonly(vertexManager().positionVertexBuffer()),
                                                                     ShaderBinding::storageBufferReadonly(vertexManager().nonPositionVertexBuffer()) });
        reg.publish("VisibilityBufferData", visBufferDataBindingSet);
    }

    // TODO: My lambda-system kind of fails horribly here. I need a reference-type for the capture to work nicely,
    //       and I also want to scope it under the if check. I either need to fix that or I'll need to make a pointer
    //       for it and then explicitly capture that pointer for this to work.
    Buffer* rtTriangleMeshBufferPtr = nullptr;
    if (m_maintainRayTracingScene) {
        // TODO: Resize the buffer if needed when more meshes are added, OR crash hard
        // TODO: Make a more reasonable default too... we need: #meshes * #LODs * #segments-per-lod
        Buffer& rtTriangleMeshBuffer = reg.createBuffer(10'000 * sizeof(RTTriangleMesh), Buffer::Usage::StorageBuffer);
        rtTriangleMeshBuffer.setStride(sizeof(RTTriangleMesh));
        rtTriangleMeshBuffer.setName("SceneRTTriangleMeshData");

        rtTriangleMeshBufferPtr = &rtTriangleMeshBuffer;

        BindingSet& rtMeshDataBindingSet = reg.createBindingSet({ ShaderBinding::storageBufferReadonly(rtTriangleMeshBuffer, ShaderStage::AnyRayTrace),
                                                                  ShaderBinding::storageBufferReadonly(vertexManager().indexBuffer(), ShaderStage::AnyRayTrace),
                                                                  ShaderBinding::storageBufferReadonly(vertexManager().positionVertexBuffer(), ShaderStage::AnyRayTrace),
                                                                  ShaderBinding::storageBufferReadonly(vertexManager().nonPositionVertexBuffer(), ShaderStage::AnyRayTrace) });
        reg.publish("SceneRTMeshDataSet", rtMeshDataBindingSet);
    }

    // Light data stuff
    Buffer& lightMetaDataBuffer = reg.createBuffer(sizeof(LightMetaData), Buffer::Usage::ConstantBuffer);
    lightMetaDataBuffer.setStride(sizeof(LightMetaData));
    lightMetaDataBuffer.setName("SceneLightMetaData");
    Buffer& dirLightDataBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::StorageBuffer);
    dirLightDataBuffer.setStride(sizeof(DirectionalLightData));
    dirLightDataBuffer.setName("SceneDirectionalLightData");
    Buffer& spotLightDataBuffer = reg.createBuffer(10 * sizeof(SpotLightData), Buffer::Usage::StorageBuffer);
    spotLightDataBuffer.setStride(sizeof(SpotLightData));
    spotLightDataBuffer.setName("SceneSpotLightData");

    BindingSet& lightBindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(lightMetaDataBuffer),
                                                         ShaderBinding::storageBufferReadonly(dirLightDataBuffer),
                                                         ShaderBinding::storageBufferReadonly(spotLightDataBuffer) });
    reg.publish("SceneLightSet", lightBindingSet);

    // Shadow resources
    Texture& directionalShadowMask = reg.createTexture2D(renderResolution, Texture::Format::R8);
    reg.publish("DirectionalLightShadowMask", directionalShadowMask);

    for (ManagedSpotLight& managedLight : m_managedSpotLights) {
        // Reset any shadow mask textures that might be set on lights, as they are no longer valid
        // (they are owned by the registry, so are reset whenever we reconstruct the pipeline)
        managedLight.shadowMaskTexture = nullptr;
        if (managedLight.shadowMaskHandle) {
            unregisterTexture(managedLight.shadowMaskHandle);
            managedLight.shadowMaskHandle.invalidate();
        }
    }

    // Misc. data
    Texture& blueNoiseTextureArray = reg.loadTextureArrayFromFileSequence("assets/engine/blue-noise/64_64/HDR_RGBA_{}.dds", false, false);
    reg.publish("BlueNoise", blueNoiseTextureArray);

    // Skinning related
    m_jointMatricesBuffer = backend().createBuffer(1024 * sizeof(mat4), Buffer::Usage::StorageBuffer);
    m_jointMatricesBuffer->setStride(sizeof(mat4));
    m_jointMatricesBuffer->setName("JointMatrixData");
    Shader skinningShader = Shader::createCompute("skinning/skinning.comp");
    BindingSet& skinningBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(m_vertexManager->positionVertexBuffer()),
                                                            ShaderBinding::storageBuffer(m_vertexManager->velocityDataVertexBuffer()),
                                                            ShaderBinding::storageBuffer(m_vertexManager->nonPositionVertexBuffer()),
                                                            ShaderBinding::storageBufferReadonly(m_vertexManager->skinningDataVertexBuffer()),
                                                            ShaderBinding::storageBufferReadonly(*m_jointMatricesBuffer) });
    StateBindings skinningStateBindings;
    skinningStateBindings.at(0, skinningBindingSet);
    ComputeState& skinningComputeState = reg.createComputeState(skinningShader, skinningStateBindings);

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
                texture->setName("Texture<" + loadedImageForTex.imageAsset->assetFilePath().generic_string() + ">");

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

        // Do mesh streaming
        {
            m_vertexManager->processMeshStreaming(cmdList, m_changedStaticMeshes);
        }

        // Update camera data
        {
            Camera const& camera = this->camera();

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

            for (auto const& skeletalMeshInstance : m_skeletalMeshInstances) {

                bool hasSkeleton = skeletalMeshInstance->hasSkeleton();

                if (hasSkeleton) {
                    std::vector<mat4> const& jointMatrices = skeletalMeshInstance->skeleton().appliedJointMatrices();
                    // std::vector<mat3> const& jointTangentMatrices = skeletalMeshInstance->skeleton().appliedJointTangentMatrices();

                    // TODO/OPTIMIZATION: Upload all instance's matrices in a single buffer once and simply offset into it!
                    uploadBuffer.upload(jointMatrices, *m_jointMatricesBuffer);
                }

                cmdList.executeBufferCopyOperations(uploadBuffer);

                // TODO: Don't do this every frame! but.. it should be safe to do so, so let's keep it so for now
                m_vertexManager->allocateSkeletalMeshInstance(*skeletalMeshInstance, cmdList);

                for (SkinningVertexMapping const& skinningVertexMapping : skeletalMeshInstance->skinningVertexMappings()) {

                    //ARKOSE_ASSERT(skinningVertexMapping.underlyingMesh.hasSkinningData());
                    ARKOSE_ASSERT(skinningVertexMapping.skinnedTarget.hasVelocityData());
                    ARKOSE_ASSERT(skinningVertexMapping.underlyingMesh.vertexCount == skinningVertexMapping.skinnedTarget.vertexCount);
                    u32 vertexCount = skinningVertexMapping.underlyingMesh.vertexCount;

                    cmdList.setNamedUniform<u32>("firstSrcVertexIdx", skinningVertexMapping.underlyingMesh.firstVertex);
                    cmdList.setNamedUniform<u32>("firstDstVertexIdx", skinningVertexMapping.skinnedTarget.firstVertex);
                    cmdList.setNamedUniform<i32>("firstSkinningVertexIdx", hasSkeleton ? skinningVertexMapping.underlyingMesh.firstSkinningVertex : -1);
                    cmdList.setNamedUniform<u32>("firstVelocityVertexIdx", static_cast<u32>(skinningVertexMapping.skinnedTarget.firstVelocityVertex));
                    cmdList.setNamedUniform<u32>("vertexCount", skinningVertexMapping.underlyingMesh.vertexCount);

                    constexpr u32 localSize = 64;
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

            size_t itemCount = staticMeshInstances().size();
            size_t batchSize = itemCount >= 512 ? 512 : 64; // if instance count is small don't go crazy with batch size
            ParallelForBatched(itemCount, batchSize, [this, &drawableCount, &instancesNeedingReinit](size_t idx) {
                auto& instance = staticMeshInstances()[idx];

                bool meshHasUpdated = m_changedStaticMeshes.contains(instance->mesh());

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

            m_changedStaticMeshes.clear();

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
            std::vector<SpotLightData> spotLightData;

            ARKOSE_ASSERTM(m_managedDirectionalLights.size() <= 1, "We only support 0 or 1 directional lights in a scene");
            for (const ManagedDirectionalLight& managedLight : m_managedDirectionalLights) {

                if (!managedLight.light) {
                    continue;
                }

                const DirectionalLight& light = *managedLight.light;

                dirLightData.emplace_back(DirectionalLightData { .color = light.color().asVec3() * light.intensityValue() * lightPreExposure(),
                                                                 .exposure = lightPreExposure(),
                                                                 .worldSpaceDirection = vec4(light.transform().forward(), 0.0),
                                                                 .viewSpaceDirection = viewFromWorld * vec4(light.transform().forward(), 0.0),
                                                                 .lightProjectionFromWorld = light.viewProjection(),
                                                                 .lightProjectionFromView = light.viewProjection() * worldFromView });
            }

            for (ManagedSpotLight& managedLight : m_managedSpotLights) {

                if (!managedLight.light) {
                    continue;
                }

                SpotLight const& light = *managedLight.light;

                i32 rtShadowMaskIndexIfActive = -1;
                if (m_maintainRayTracingScene && light.shadowMode() == ShadowMode::RayTraced) {

                    // NOTE: If you change a light from RT to shadow-mapped, we currently leak the texture!
                    // As it's managed by the Registry, it will get cleaned up when we reconstruct or destroy the pipeline,
                    // but never outside of that. Not a massive deal, but worth keeping in mind! Ideally we'd keep a pool
                    // of them or maybe just delete it right away.
                    if (managedLight.shadowMaskTexture == nullptr) {
                        managedLight.shadowMaskTexture = &reg.createTexture2D(renderResolution, Texture::Format::R8);

                        managedLight.shadowMaskHandle = registerTextureSlot();
                        updateTextureUnowned(managedLight.shadowMaskHandle, managedLight.shadowMaskTexture);
                    }

                    rtShadowMaskIndexIfActive = managedLight.shadowMaskHandle.indexOfType<int>();
                }

                spotLightData.emplace_back(SpotLightData { .color = light.color().asVec3() * light.intensityValue() * lightPreExposure(),
                                                           .exposure = lightPreExposure(),
                                                           .worldSpaceDirection = vec4(light.transform().forward(), 0.0),
                                                           .viewSpaceDirection = viewFromWorld * vec4(light.transform().forward(), 0.0),
                                                           .lightProjectionFromWorld = light.viewProjection(),
                                                           .lightProjectionFromView = light.viewProjection() * worldFromView,
                                                           .worldSpaceRight = vec4(light.transform().right(), 0.0),
                                                           .worldSpaceUp = vec4(light.transform().up(), 0.0),
                                                           .viewSpaceRight = viewFromWorld * vec4(light.transform().right(), 0.0),
                                                           .viewSpaceUp = viewFromWorld * vec4(light.transform().up(), 0.0),
                                                           .worldSpacePosition = vec4(light.transform().positionInWorld(), 0.0f),
                                                           .viewSpacePosition = viewFromWorld * vec4(light.transform().positionInWorld(), 1.0f),
                                                           .outerConeHalfAngle = light.outerConeAngle() / 2.0f,
                                                           .iesProfileIndex = managedLight.iesLut.indexOfType<int>(),
                                                           .rtShadowMaskIndex = rtShadowMaskIndexIfActive,
                                                           ._pad0 = 0 });
            }

            uploadBuffer.upload(dirLightData, dirLightDataBuffer);
            uploadBuffer.upload(spotLightData, spotLightDataBuffer);

            LightMetaData metaData { .hasDirectionalLight = dirLightData.size() > 0 ? true : false,
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

                            if (meshSegment.blas == nullptr) { 
                                // Not yet loaded
                                continue;
                            }

                            u32 rtMeshIndex = narrow_cast<u32>(rayTracingMeshData.size());

                            DrawCallDescription drawCallDesc = DrawCallDescription::fromVertexAllocation(meshSegment.vertexAllocation);
                            rayTracingMeshData.push_back(RTTriangleMesh { .firstVertex = drawCallDesc.vertexOffset,
                                                                          .firstIndex = static_cast<int>(drawCallDesc.firstIndex),
                                                                          .materialIndex = meshSegment.material.indexOfType<int>() });

                            tlasBuildType = AccelerationStructureBuildType::FullBuild; // TODO: Only do a full rebuild sometimes!

                            uint8_t hitMask = 0x00;
                            uint32_t sbtOffset = 0;
                            if (const ShaderMaterial* material = materialForHandle(meshSegment.material)) {
                                switch (material->blendMode) {
                                case BLEND_MODE_OPAQUE:
                                    hitMask = RT_HIT_MASK_OPAQUE;
                                    sbtOffset = 0;
                                    break;
                                case BLEND_MODE_MASKED:
                                    hitMask = RT_HIT_MASK_MASKED;
                                    sbtOffset = 1;
                                    break;
                                case BLEND_MODE_TRANSLUCENT:
                                    hitMask = RT_HIT_MASK_BLEND;
                                    sbtOffset = 2;
                                    break;
                                default:
                                    ASSERT_NOT_REACHED();
                                }
                            }
                            ARKOSE_ASSERT(hitMask != 0);

                            // TODO: Probably create a geometry per mesh but only a single instance per model, and use the SBT for material lookup!
                            rayTracingGeometryInstances.push_back(RTGeometryInstance { .blas = meshSegment.blas.get(),
                                                                                       .transform = &instance->transform(),
                                                                                       .shaderBindingTableOffset = sbtOffset,
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

                            if (!instance->hasBlasForSegmentIndex(segmentIdx)) {
                                // Not yet loaded
                                continue;
                            }

                            u32 rtMeshIndex = narrow_cast<u32>(rayTracingMeshData.size());

                            DrawCallDescription drawCallDesc = DrawCallDescription::fromVertexAllocation(meshSegment.vertexAllocation);
                            rayTracingMeshData.push_back(RTTriangleMesh { .firstVertex = drawCallDesc.vertexOffset,
                                                                            .firstIndex = static_cast<int>(drawCallDesc.firstIndex),
                                                                            .materialIndex = meshSegment.material.indexOfType<int>() });

                            BottomLevelAS const& blas = *instance->blasForSegmentIndex(segmentIdx);

                            tlasBuildType = AccelerationStructureBuildType::FullBuild; // TODO: Only do a full rebuild sometimes!

                            uint8_t hitMask = 0x00;
                            uint32_t sbtOffset = 0;
                            if (const ShaderMaterial* material = materialForHandle(meshSegment.material)) {
                                switch (material->blendMode) {
                                case BLEND_MODE_OPAQUE:
                                    hitMask = RT_HIT_MASK_OPAQUE;
                                    sbtOffset = 0;
                                    break;
                                case BLEND_MODE_MASKED:
                                    hitMask = RT_HIT_MASK_MASKED;
                                    sbtOffset = 1;
                                    break;
                                case BLEND_MODE_TRANSLUCENT:
                                    hitMask = RT_HIT_MASK_BLEND;
                                    sbtOffset = 2;
                                    break;
                                default:
                                    ASSERT_NOT_REACHED();
                                }
                            }
                            ARKOSE_ASSERT(hitMask != 0);

                            // TODO: Probably create a geometry per mesh but only a single instance per model, and use the SBT for material lookup!
                            rayTracingGeometryInstances.push_back(RTGeometryInstance { .blas = &blas,
                                                                                       .transform = &instance->transform(),
                                                                                       .shaderBindingTableOffset = sbtOffset,
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
            m_environmentMapTexture->setData(imageAsset->pixelDataForMip(0).data(), imageAsset->pixelDataForMip(0).size(), 0, 0);
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

void GpuScene::updateColorGradingLUT(CubeLUT const& lut)
{
    SCOPED_PROFILE_ZONE();

    Texture::Description lutDesc{};
    lutDesc.format = Texture::Format::RGBA32F; // (a-channel unused)
    lutDesc.filter = Texture::Filters::linear();
    lutDesc.wrapMode = ImageWrapModes::clampAllToEdge();

    if (lut.is1d()) {
        lutDesc.type = Texture::Type::Texture2D;
        lutDesc.extent = { lut.tableSize(), 1, 1 };
    } else if (lut.is3d()) {
        lutDesc.type = Texture::Type::Texture3D;
        lutDesc.extent = { lut.tableSize(), lut.tableSize(), lut.tableSize() };
    }

    m_colorGradingLutTexture = backend().createTexture(lutDesc);

    std::span<const float> lutData = lut.dataForGpuUpload();
    m_colorGradingLutTexture->setData(lutData.data(), lutData.size() * sizeof(float), 0, 0);

    static int nextLutIdx = 0;
    m_colorGradingLutTexture->setName("ColorGradeLUT<" + std::to_string(nextLutIdx++) + ">");
}

Texture const& GpuScene::colorGradingLUT() const
{
    return *m_colorGradingLutTexture;
}

void GpuScene::registerLight(DirectionalLight& light)
{
    if (m_managedDirectionalLights.size() > 0) {
        ARKOSE_LOG(Error, "Registering a directional light but there's already one present. "
                          "We only support a single directional light, throwing out the old one.");
        m_managedDirectionalLights.clear();
    }

    ManagedDirectionalLight managedLight { .light = &light };
    m_managedDirectionalLights.push_back(managedLight);
}

void GpuScene::registerLight(SpotLight& light)
{
    // Default to using ray traced shadows if possible, for nice soft shadows :)
    if (m_maintainRayTracingScene && light.shadowMode() != ShadowMode::None) {
        light.setShadowMode(ShadowMode::RayTraced);
        light.setLightSourceRadius(0.175f);
    }

    TextureHandle iesLutHandle {};
    if (light.hasIesProfile()) {
        IESProfile const& iesProfile = light.iesProfile();
        constexpr u32 size = 256;

        std::vector<float> pixels = iesProfile.assembleLookupTextureData<float>(size);

        Texture::Description iesLutDesc { .type = Texture::Type::Texture2D,
                                          .arrayCount = 1u,
                                          .extent = { size, size, 1 },
                                          .format = Texture::Format::R32F,
                                          .filter = Texture::Filters::linear(),
                                          .wrapMode = ImageWrapModes::clampAllToEdge(),
                                          .mipmap = Texture::Mipmap::None,
                                          .multisampling = Texture::Multisampling::None };

        auto iesLut = backend().createTexture(iesLutDesc);

        uint8_t* data = reinterpret_cast<uint8_t*>(pixels.data());
        size_t byteSize = pixels.size() * sizeof(float);
        iesLut->setData(data, byteSize, 0, 0);

        iesLut->setName("IES-LUT:" + iesProfile.path().generic_string());
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
    auto skeleton = managedSkeletalMesh.skeletonAsset ? std::make_unique<Skeleton>(managedSkeletalMesh.skeletonAsset) : nullptr;

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

    if (meshAsset == nullptr) {
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
        m_vertexManager->registerForStreaming(skeletalMesh->underlyingMesh(), includeIndices, includeSkinningData);
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
        m_vertexManager->registerForStreaming(*staticMesh, includeIndices, includeSkinningData);
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

void GpuScene::notifyStaticMeshHasChanged(StaticMeshHandle handle)
{
    m_changedStaticMeshes.insert(handle);
}

MaterialHandle GpuScene::registerMaterial(MaterialAsset const* materialAsset)
{
    SCOPED_PROFILE_ZONE();

    // NOTE: A material in this context is very lightweight (for now) so we don't cache them

    // Register textures / material inputs
    TextureHandle baseColor = registerMaterialTexture(materialAsset->baseColor, ImageType::sRGBColor, m_whiteTexture.get());
    TextureHandle emissive = registerMaterialTexture(materialAsset->emissiveColor, ImageType::sRGBColor, m_whiteTexture.get());
    TextureHandle normalMap = registerMaterialTexture(materialAsset->normalMap, ImageType::NormalMap, m_normalMapBlueTexture.get());
    TextureHandle bentNormalMap = registerMaterialTexture(materialAsset->bentNormalMap, ImageType::NormalMap, m_whiteTexture.get());
    TextureHandle metallicRoughness = registerMaterialTexture(materialAsset->materialProperties, ImageType::GenericData, m_whiteTexture.get());
    TextureHandle occlusionMap = registerMaterialTexture(materialAsset->occlusionMap, ImageType::GenericData, m_whiteTexture.get());

    ShaderMaterial shaderMaterial {};

    shaderMaterial.baseColor = baseColor.indexOfType<int>();
    shaderMaterial.normalMap = normalMap.indexOfType<int>();
    shaderMaterial.bentNormalMap = bentNormalMap.indexOfType<int>();
    shaderMaterial.metallicRoughness = metallicRoughness.indexOfType<int>();
    shaderMaterial.emissive = emissive.indexOfType<int>();
    shaderMaterial.occlusion = occlusionMap.indexOfType<int>();

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

    auto translateBrdfShaderMaterial = [](Brdf brdf) -> int {
        switch (brdf) {
        case Brdf::Default:
            return BRDF_DEFAULT;
        case Brdf::Skin:
            return BRDF_SKIN;
        default:
            ASSERT_NOT_REACHED();
        }
    };

    shaderMaterial.blendMode = translateBlendModeToShaderMaterial(materialAsset->blendMode);
    shaderMaterial.maskCutoff = materialAsset->maskCutoff;

    shaderMaterial.brdf = translateBrdfShaderMaterial(materialAsset->brdf);

    shaderMaterial.metallicFactor = materialAsset->metallicFactor;
    shaderMaterial.roughnessFactor = materialAsset->roughnessFactor;
    shaderMaterial.emissiveFactor = materialAsset->emissiveFactor;

    shaderMaterial.colorTint = materialAsset->colorTint;

    shaderMaterial.clearcoat = materialAsset->clearcoat;
    shaderMaterial.clearcoatRoughness = materialAsset->clearcoatRoughness;

    shaderMaterial.dielectricReflectance = materialAsset->calculateDielectricReflectance();

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

    if (!input.has_value()) {
        ARKOSE_ASSERT(fallback != nullptr);
        auto fallbackEntry = m_materialFallbackTextureCache.find(fallback);
        if (fallbackEntry == m_materialFallbackTextureCache.end()) {
            TextureHandle handle = registerTextureSlot();
            m_managedTextures.markPersistent(handle);
            updateTextureUnowned(handle, fallback);
            m_materialFallbackTextureCache[fallback] = handle;
            return handle;
        } else {
            TextureHandle handle = fallbackEntry->second;
            return handle;
        }
    }

    auto entry = m_materialTextureCache.find(input.value());
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
        m_materialTextureCache[input.value()] = handle;

        std::string const& imageAssetPath = input->image;

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
                    texture->setData(imageAsset->pixelDataForMip(0).data(), imageAsset->pixelDataForMip(0).size(), 0, 0);
                }

                if (textureWantMips) {
                    if (assetHasMips) {
                        for (size_t mipIdx = 0; mipIdx < imageAsset->numMips(); ++mipIdx) {
                            std::span<const u8> mipPixelData = imageAsset->pixelDataForMip(mipIdx);
                            texture->setData(mipPixelData.data(), mipPixelData.size(), mipIdx, 0);
                        }
                    } else {
                        texture->generateMipmaps();
                    }
                }

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

        // TODO!
        //m_vertexManager->unregisterFromStreaming(*managedStaticMesh.staticMesh);

        managedStaticMesh.meshAsset = nullptr;
        managedStaticMesh.staticMesh.reset();
    });

    m_managedMaterials.processDeferredDeletes(m_currentFrameIdx, DeferredFrames, [this](MaterialHandle handle, ShaderMaterial& shaderMaterial) {
        // Unregister dependencies (textures)
        unregisterTexture(TextureHandle(shaderMaterial.baseColor));
        unregisterTexture(TextureHandle(shaderMaterial.emissive));
        unregisterTexture(TextureHandle(shaderMaterial.normalMap));
        unregisterTexture(TextureHandle(shaderMaterial.bentNormalMap));
        unregisterTexture(TextureHandle(shaderMaterial.metallicRoughness));

        shaderMaterial = ShaderMaterial();
        m_pendingMaterialUpdates.push_back(handle);
    });

    m_managedTextures.processDeferredDeletes(m_currentFrameIdx, DeferredFrames, [this](TextureHandle handle, std::unique_ptr<Texture>& texture) {
        // NOTE: Currently we can put null textures in the list if there is no texture, meaning we still reserve a texture slot and we have to handle that here.
        // TODO: Perhaps this isn't ideal? Consider if we can avoid reserving one altogether..
        if (texture != nullptr) {
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

bool GpuScene::hasPendingUploads() const
{
    // This isn't entirely foolproof, but it's something
    return m_asyncLoadedImages.size() > 0 || m_pendingTextureUpdates.size() > 0 || m_pendingMaterialUpdates.size() > 0;
}

void GpuScene::drawResourceUI()
{
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
                ImGui::Text("%s", heapNames.back().c_str());

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
            for (size_t i = 0; i < stats.heaps.size(); ++i) {
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

    ImGui::Text("Managed resources:");
    ImGui::Columns(4);
    ImGui::Text("static meshes: %u", narrow_cast<i32>(m_managedStaticMeshes.size()));
    ImGui::NextColumn();
    ImGui::Text("skeletal meshes: %u", narrow_cast<i32>(m_managedSkeletalMeshes.size()));
    ImGui::NextColumn();
    ImGui::Text("materials: %u", narrow_cast<i32>(m_managedMaterials.size()));
    ImGui::NextColumn();
    ImGui::Text("textures: %u", narrow_cast<i32>(m_managedTextures.size()));
    ImGui::Columns(1);

    ImGui::Separator();

    if (ImGui::BeginTabBar("VramUsageBreakdown")) {

        if (ImGui::BeginTabItem("Vertex manager")) {
            vertexManager().drawUI();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Managed textures")) {

            ImGui::Text("Number of managed textures: %u", narrow_cast<int>(m_managedTextures.size()));

            size_t compressedTotalVRAM = 0;
            size_t uncompressedTotalVRAM = 0;

            m_managedTextures.forEachResource([&](std::unique_ptr<Texture> const& texture) {
                if (texture) {
                    if (texture->hasBlockCompressedFormat()) {
                        compressedTotalVRAM += texture->sizeInMemory();
                    } else {
                        uncompressedTotalVRAM += texture->sizeInMemory();
                    }
                }
            });

            float managedTexturesTotalGB = ark::conversion::to::GB(compressedTotalVRAM + uncompressedTotalVRAM);
            ImGui::Text("Using %.2f GB", managedTexturesTotalGB);

            ImGui::Text("Compressed:   %.2f GB", ark::conversion::to::GB(compressedTotalVRAM));
            ImGui::Text("Uncompressed: %.2f GB", ark::conversion::to::GB(uncompressedTotalVRAM));

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

