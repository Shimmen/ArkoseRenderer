#pragma once

#include "core/Handle.h"
#include "core/parallel/TaskGraph.h"
#include "rendering/RenderPipelineNode.h"
#include "scene/Scene.h"
#include "scene/camera/Camera.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

// Shared shader data
using uint = uint32_t;
#include "LightData.h"
#include "SceneData.h"
#include "RTData.h"

class DirectionalLight;
class Light;
class Mesh;
class SpotLight;

DEFINE_HANDLE_TYPE(TextureHandle);

class GpuScene final : public RenderPipelineNode {
public:
    GpuScene(Scene&, Backend&, Extent2D initialMainViewportSize);

    void initialize(Badge<Scene>, bool rayTracingCapable);

    // Render asset accessors

    Backend& backend() { return m_backend; }
    const Backend& backend() const { return m_backend; }

    Scene& scene() { return m_scene; }
    const Scene& scene() const { return m_scene; }

    Camera& camera() { return scene().camera(); }
    const Camera& camera() const { return scene().camera(); }

    size_t meshCount() const { return m_managedStaticMeshes.size(); }
    size_t forEachStaticMesh(std::function<void(size_t, StaticMesh&)> callback);

    StaticMesh* staticMeshForHandle(StaticMeshHandle handle);
    const StaticMesh* staticMeshForHandle(StaticMeshHandle handle) const;
    const Material* materialForHandle(MaterialHandle handle) const;

    // TODO: This is a temporary helper, remove me eventually!
    void ensureDrawCallIsAvailableForAll(VertexLayout);

    size_t lightCount() const;
    size_t shadowCastingLightCount() const;
    size_t forEachShadowCastingLight(std::function<void(size_t, Light&)>);
    size_t forEachShadowCastingLight(std::function<void(size_t, const Light&)>) const;
    size_t forEachLocalLight(std::function<void(size_t, Light&)>);
    size_t forEachLocalLight(std::function<void(size_t, const Light&)>) const;

    // RenderPipelineNode interface

    std::string name() const override { return "Scene"; }
    RenderPipelineNode::ExecuteCallback construct(GpuScene&, Registry&) override;

    // GPU data registration

    void registerLight(SpotLight&);
    void registerLight(DirectionalLight&);
    // TODO: Unregister light!

    StaticMeshHandle registerStaticMesh(std::shared_ptr<StaticMesh>);
    // TODO: void unregisterStaticMesh(StaticMeshHandle);

    [[nodiscard]] MaterialHandle registerMaterial(Material&);
    void unregisterMaterial(MaterialHandle);

    [[nodiscard]] TextureHandle registerMaterialTexture(Material::TextureDescription&);
    [[nodiscard]] TextureHandle registerTexture(std::unique_ptr<Texture>&&);
    [[nodiscard]] TextureHandle registerTextureSlot();
    void updateTexture(TextureHandle, std::unique_ptr<Texture>&&);
    void updateTextureUnowned(TextureHandle, Texture*);
    void unregisterTexture(TextureHandle);

    // Lighting & environment

    float lightPreExposure() const { return m_lightPreExposure; }
    float preExposedAmbient() const { return scene().ambientIlluminance() * lightPreExposure(); }
    float preExposedEnvironmentBrightnessFactor() const { return scene().environmentMap().brightnessFactor * lightPreExposure(); }

    void updateEnvironmentMap(Scene::EnvironmentMap&);
    Texture& environmentMapTexture();

    // Managed GPU assets

    DrawCallDescription fitVertexAndIndexDataForMesh(Badge<StaticMeshSegment>, const StaticMeshSegment&, const VertexLayout&, std::optional<DrawCallDescription> alignWith = {});

    Buffer& globalVertexBufferForLayout(const VertexLayout&) const;
    Buffer& globalIndexBuffer() const;
    IndexType globalIndexBufferType() const;

    BindingSet& globalMaterialBindingSet() const;

    TopLevelAS& globalTopLevelAccelerationStructure() const;

    // Misc.

    void drawStatsGui(bool includeContainingWindow = false);
    void drawVramUsageGui(bool includeContainingWindow = false);

private:
    Scene& m_scene;
    Backend& m_backend;

    bool m_maintainRayTracingScene { false };
    // NOTE: It's possible some RT pass would want more vertex info than this, but in all cases I can think of
    // we want either these and nothing more, or nothing at all (e.g. ray traced AO). Remember that vertex positions
    // are always available more directly, as we know our hit point.
    const VertexLayout m_rayTracingVertexLayout = { VertexComponent::Normal3F,
                                                    VertexComponent::TexCoord2F };

    // TODO: Create a geometry per mesh (or rather, per LOD) and use the SBT to lookup material.
    // For now we create one per segment so we can ensure one material per "draw"
    std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(StaticMeshSegment&, uint32_t meshIdx);

    float m_lightPreExposure { 1.0f };

    // GPU data

    std::unique_ptr<Buffer> m_global32BitIndexBuffer { nullptr };
    uint32_t m_nextFreeIndex { 0 };

    std::unordered_map<VertexLayout, std::unique_ptr<Buffer>> m_globalVertexBuffers {};
    uint32_t m_nextFreeVertexIndex { 0 };

    struct ManagedStaticMesh {
        std::shared_ptr<StaticMesh> staticMesh {};
    };
    std::vector<ManagedStaticMesh> m_managedStaticMeshes {};

    struct ManagedDirectionalLight {
        DirectionalLight* light {};
    };
    std::vector<ManagedDirectionalLight> m_managedDirectionalLights {};

    struct ManagedSpotLight {
        SpotLight* light {};
        TextureHandle iesLut {};
    };
    std::vector<ManagedSpotLight> m_managedSpotLights {};

    struct ManagedTexture {
        std::unique_ptr<Texture> texture {};
        uint64_t referenceCount { 0 };
    };
    std::vector<ManagedTexture> m_managedTextures {};
    std::unordered_map<Material::TextureDescription, TextureHandle> m_materialTextureCache {};
    std::vector<BindingSet::TextureBindingUpdate> m_pendingTextureUpdates {};
    static constexpr int MaxSupportedSceneTextures = 4096;

    static constexpr bool UseAsyncTextureLoads = true;
    static constexpr size_t MaxNumAsyncTextureLoadsToFinalizePerFrame = 4;
    struct LoadedImageForTextureCreation {
        Image* image {};
        std::string path {};
        TextureHandle textureHandle {};
        Texture::Description textureDescription {};
    };
    std::mutex m_asyncLoadedImagesMutex {};
    std::vector<LoadedImageForTextureCreation> m_asyncLoadedImages {};
    

    struct ManagedMaterial {
        Material sourceMaterial;
        ShaderMaterial shaderMaterial {};
        uint64_t referenceCount { 0 };
    };
    std::vector<ManagedMaterial> m_managedMaterials {};
    static constexpr int MaxSupportedSceneMaterials = 1'000;
    std::unique_ptr<Buffer> m_materialDataBuffer { nullptr };
    std::vector<uint32_t> m_pendingMaterialUpdates {};

    // NOTE: Currently this contains both textures and material data
    static constexpr int MaterialBindingSetBindingIndexMaterials = 0;
    static constexpr int MaterialBindingSetBindingIndexTextures = 1;
    std::unique_ptr<BindingSet> m_materialBindingSet { nullptr };

    std::vector<std::unique_ptr<BottomLevelAS>> m_allBottomLevelAccelerationStructures {};

    static constexpr uint32_t InitialMaxRayTracingGeometryInstanceCount { 1024 };
    std::unique_ptr<TopLevelAS> m_sceneTopLevelAccelerationStructure {};
    uint32_t m_framesUntilNextFullTlasBuild { 0u };

    std::unique_ptr<Texture> m_environmentMapTexture {};

    // Common textures that can be used for various purposes
    std::unique_ptr<Texture> m_blackTexture {};
    std::unique_ptr<Texture> m_lightGrayTexture {};
    std::unique_ptr<Texture> m_magentaTexture {};
    std::unique_ptr<Texture> m_normalMapBlueTexture {};

    // GPU management

    using VramUsageAvgAccumulatorType = AvgAccumulator<float, 20>;
    std::vector<VramUsageAvgAccumulatorType> m_vramUsageHistoryPerHeap {};

    size_t m_managedTexturesVramUsage { 0 };
    size_t m_totalBlasVramUsage { 0 };
    size_t m_totalNumBlas { 0 };
};
