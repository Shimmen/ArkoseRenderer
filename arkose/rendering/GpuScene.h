#pragma once

#include <ark/handle.h>
#include "core/Types.h"
#include "core/parallel/TaskGraph.h"
#include "rendering/meshlet/MeshletManager.h"
#include "rendering/Drawable.h"
#include "rendering/IconManager.h"
#include "rendering/RenderPipelineNode.h"
#include "rendering/ResourceList.h"
#include "rendering/SkeletalMesh.h"
#include "rendering/StaticMesh.h"
#include "rendering/VertexManager.h"
#include "scene/Scene.h"
#include "scene/camera/Camera.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

// Shared shader data
#include "shaders/shared/LightData.h"
#include "shaders/shared/MaterialData.h"
#include "shaders/shared/SceneData.h"
#include "shaders/shared/RTData.h"

class DirectionalLight;
class Light;
class MaterialAsset;
class Mesh;
class MeshAsset;
class SkeletonAsset;
class SpotLight;

ARK_DEFINE_HANDLE_TYPE(TextureHandle);

class GpuScene final : public RenderPipelineNode {
public:
    GpuScene(Scene&, Backend&);

    void initialize(Badge<Scene>, bool rayTracingCapable, bool meshShadingCapable);

    void preRender();
    void postRender();

    // Render asset accessors

    Backend& backend() { return m_backend; }
    const Backend& backend() const { return m_backend; }

    Scene& scene() { return m_scene; }
    const Scene& scene() const { return m_scene; }

    Camera& camera() { return scene().camera(); }
    const Camera& camera() const { return scene().camera(); }

    size_t meshCount() const { return m_managedStaticMeshes.size(); }

    SkeletalMesh* skeletalMeshForInstance(SkeletalMeshInstance const&);
    SkeletalMesh const* skeletalMeshForInstance(SkeletalMeshInstance const&) const;
    SkeletalMesh* skeletalMeshForHandle(SkeletalMeshHandle);
    SkeletalMesh const* skeletalMeshForHandle(SkeletalMeshHandle) const;

    StaticMesh* staticMeshForInstance(StaticMeshInstance const&);
    StaticMesh const* staticMeshForInstance(StaticMeshInstance const&) const;
    StaticMesh* staticMeshForHandle(StaticMeshHandle handle);
    const StaticMesh* staticMeshForHandle(StaticMeshHandle handle) const;
    const ShaderMaterial* materialForHandle(MaterialHandle handle) const;
    ShaderDrawable const* drawableForHandle(DrawableObjectHandle handle) const;

    size_t lightCount() const;
    size_t shadowCastingLightCount() const;
    size_t forEachShadowCastingLight(std::function<void(size_t, Light&)>);
    size_t forEachShadowCastingLight(std::function<void(size_t, const Light&)>) const;
    size_t forEachLocalLight(std::function<void(size_t, Light&)>);
    size_t forEachLocalLight(std::function<void(size_t, const Light&)>) const;

    // RenderPipelineNode interface

    std::string name() const override { return "Scene"; }

    void drawGui() override;
    RenderPipelineNode::ExecuteCallback construct(GpuScene&, Registry&) override;

    // GPU data registration

    void registerLight(DirectionalLight&);
    void registerLight(SphereLight&);
    void registerLight(SpotLight&);
    // TODO: Unregister light!

    SkeletalMeshInstance& createSkeletalMeshInstance(SkeletalMeshHandle, Transform);
    void initializeSkeletalMeshInstance(SkeletalMeshInstance&);

    StaticMeshInstance& createStaticMeshInstance(StaticMeshHandle, Transform);
    void initializeStaticMeshInstance(StaticMeshInstance&);

    std::vector<std::unique_ptr<StaticMeshInstance>>& staticMeshInstances() { return m_staticMeshInstances; }
    const std::vector<std::unique_ptr<StaticMeshInstance>>& staticMeshInstances() const { return m_staticMeshInstances; }

    std::vector<std::unique_ptr<SkeletalMeshInstance>>& skeletalMeshInstances() { return m_skeletalMeshInstances; }
    const std::vector<std::unique_ptr<SkeletalMeshInstance>>& skeletalMeshInstances() const { return m_skeletalMeshInstances; }

    // NOTE: This is more of a utility for now to clear out the current level
    void clearAllMeshInstances();

    // TODO: Later, also count skeletal meshes here
    uint32_t meshInstanceCount() const { return static_cast<uint32_t>(m_staticMeshInstances.size()); }

    SkeletalMeshHandle registerSkeletalMesh(MeshAsset const*, SkeletonAsset const*);
    void unregisterSkeletalMesh(SkeletalMeshHandle);

    StaticMeshHandle registerStaticMesh(MeshAsset const*);
    void unregisterStaticMesh(StaticMeshHandle);

    [[nodiscard]] MaterialHandle registerMaterial(MaterialAsset const*);
    void unregisterMaterial(MaterialHandle);

    [[nodiscard]] TextureHandle registerMaterialTexture(std::optional<MaterialInput> const&, ImageType, Texture* fallback);
    [[nodiscard]] TextureHandle registerTexture(std::unique_ptr<Texture>&&);
    [[nodiscard]] TextureHandle registerTextureSlot();
    void updateTexture(TextureHandle, std::unique_ptr<Texture>&&);
    void updateTextureUnowned(TextureHandle, Texture*);
    void unregisterTexture(TextureHandle);

    // Lighting & environment

    float lightPreExposure() const { return m_lightPreExposure; }
    float preExposedAmbient() const { return scene().ambientIlluminance() * lightPreExposure(); }
    float preExposedEnvironmentBrightnessFactor() const { return scene().environmentMap().brightnessFactor * lightPreExposure(); }

    void updateEnvironmentMap(EnvironmentMap&);
    Texture& environmentMapTexture();

    float globalMipBias() const { return m_globalMipBias; }

    bool shouldIncludeMaterialColor() const { return m_includeMaterialColor; }
    bool& shouldIncludeMaterialColorMutable() { return m_includeMaterialColor; }

    // Managed GPU assets

    void processDeferredDeletions();

    BindingSet& globalMaterialBindingSet() const;

    TopLevelAS& globalTopLevelAccelerationStructure() const;

    Texture const& blackTexture() const { return *m_blackTexture; }
    Texture const& whiteTexture() const { return *m_whiteTexture; }
    Texture const& lightGrayTexture() const { return *m_lightGrayTexture; }
    Texture const& magentaTexture() const { return *m_magentaTexture; }
    Texture const& normalMapBlueTexture() const { return *m_normalMapBlueTexture; }

    IconManager const& iconManager() const { return *m_iconManager; }

    // Mesh / vertex related

    VertexManager const& vertexManager() const;

    // Meshlet / mesh shading related

    MeshletManager const& meshletManager() const;

    // Misc.

    void drawStatsGui(bool includeContainingWindow = false);
    void drawVramUsageGui(bool includeContainingWindow = false);

    size_t drawableCountForFrame() const { return m_drawableCountForFrame; }

public:
    // (public visibility needed for natvis inspection)

    struct ManagedStaticMesh {
        MeshAsset const* meshAsset {};
        std::unique_ptr<StaticMesh> staticMesh {};
    };

    struct ManagedSkeletalMesh {
        MeshAsset const* meshAsset {};
        SkeletonAsset const* skeletonAsset {};
        std::unique_ptr<SkeletalMesh> skeletalMesh {};
    };

private:
    Scene& m_scene;
    Backend& m_backend;

    bool m_maintainRayTracingScene { false };
    bool m_meshShadingCapable { false };

    float m_lightPreExposure { 1.0f };

    float m_globalMipBias { 0.0f };

    bool m_includeMaterialColor { true };

    // GPU data

    ResourceList<ManagedSkeletalMesh, SkeletalMeshHandle> m_managedSkeletalMeshes { "Skeletal Meshes", 128 };

    ResourceList<ManagedStaticMesh, StaticMeshHandle> m_managedStaticMeshes { "Static Meshes", 1024 };
    std::unordered_map<MeshAsset const*, StaticMeshHandle> m_staticMeshAssetCache {};

    std::vector<std::unique_ptr<SkeletalMeshInstance>> m_skeletalMeshInstances {};
    std::vector<std::unique_ptr<StaticMeshInstance>> m_staticMeshInstances {};
    ResourceList<ShaderDrawable, DrawableObjectHandle> m_drawables { "Drawables", 65'536 };

    std::unique_ptr<VertexManager> m_vertexManager {};
    std::unique_ptr<MeshletManager> m_meshletManager {};

    struct ManagedDirectionalLight {
        DirectionalLight* light {};
    };
    std::vector<ManagedDirectionalLight> m_managedDirectionalLights {};

    struct ManagedSphereLight {
        SphereLight* light {};
    };
    std::vector<ManagedSphereLight> m_managedSphereLights {};

    struct ManagedSpotLight {
        SpotLight* light {};
        TextureHandle iesLut {};
    };
    std::vector<ManagedSpotLight> m_managedSpotLights {};

    ResourceList<std::unique_ptr<Texture>, TextureHandle> m_managedTextures { "Textures", 4096 };
    // TODO: This key should probably be not just the path but also some meta-info, e.g. what wrap modes we want!
    std::unordered_map<std::string, TextureHandle> m_materialTextureCache {};
    std::unordered_map<Texture*, TextureHandle> m_materialFallbackTextureCache {};
    std::vector<BindingSet::TextureBindingUpdate> m_pendingTextureUpdates {};

    static constexpr bool UseAsyncTextureLoads = true;
    struct LoadedImageForTextureCreation {
        ImageAsset* imageAsset { nullptr };
        TextureHandle textureHandle {};
        Texture::Description textureDescription {};
    };
    std::mutex m_asyncLoadedImagesMutex {};
    std::vector<LoadedImageForTextureCreation> m_asyncLoadedImages {};

    ResourceList<ShaderMaterial, MaterialHandle> m_managedMaterials { "Materials", 10'000 };
    std::unique_ptr<Buffer> m_materialDataBuffer { nullptr };
    std::vector<MaterialHandle> m_pendingMaterialUpdates {};
    MaterialHandle m_defaultMaterialHandle {};

    // NOTE: Currently this contains both textures and material data
    static constexpr int MaterialBindingSetBindingIndexMaterials = 0;
    static constexpr int MaterialBindingSetBindingIndexTextures = 1;
    std::unique_ptr<BindingSet> m_materialBindingSet { nullptr };

    static constexpr uint32_t InitialMaxRayTracingGeometryInstanceCount { 32'768 };
    std::unique_ptr<TopLevelAS> m_sceneTopLevelAccelerationStructure {};
    uint32_t m_framesUntilNextFullTlasBuild { 0u };

    std::unique_ptr<Texture> m_environmentMapTexture {};

    // Skinning related data
    std::unique_ptr<Buffer> m_jointMatricesBuffer {};

    // Common buffers that can be used
    std::unique_ptr<Buffer> m_emptyVertexBuffer {};
    std::unique_ptr<Buffer> m_emptyIndexBuffer {};

    // Common textures that can be used for various purposes
    std::unique_ptr<Texture> m_blackTexture {};
    std::unique_ptr<Texture> m_whiteTexture {};
    std::unique_ptr<Texture> m_lightGrayTexture {};
    std::unique_ptr<Texture> m_magentaTexture {};
    std::unique_ptr<Texture> m_normalMapBlueTexture {};

    std::unique_ptr<IconManager> m_iconManager {};

    // GPU management

    uint32_t m_currentFrameIdx { 0 };

    using VramUsageAvgAccumulatorType = AvgAccumulator<float, 20>;
    std::vector<VramUsageAvgAccumulatorType> m_vramUsageHistoryPerHeap {};

    size_t m_drawableCountForFrame { 0 };

    size_t m_managedTexturesVramUsage { 0 };
    //size_t m_totalBlasVramUsage { 0 };
    //size_t m_totalNumBlas { 0 };
};

#if defined(PLATFORM_WINDOWS)
// For use by natvis - please don't access these for anything else :^)
static ResourceList<GpuScene::ManagedStaticMesh, StaticMeshHandle>* natvis_managedStaticMeshes { nullptr };
static ResourceList<std::unique_ptr<Texture>, TextureHandle>* natvis_managedTextures { nullptr };
#endif // defined(PLATFORM_WINDOWS)
