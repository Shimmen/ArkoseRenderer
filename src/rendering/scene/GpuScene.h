#pragma once

#include "core/Handle.h"
#include "core/parallel/TaskGraph.h"
#include "rendering/RenderPipelineNode.h"
#include "rendering/camera/Camera.h"
#include "rendering/scene/Scene.h"
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
class Mesh;
class SpotLight;

DEFINE_HANDLE_TYPE(TextureHandle);
DEFINE_HANDLE_TYPE(MaterialHandle);

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

    size_t meshCount() const { return m_managedMeshes.size(); }
    size_t forEachMesh(std::function<void(size_t, Mesh&)> callback);
    size_t forEachMesh(std::function<void(size_t, const Mesh&)> callback) const;

    size_t lightCount() const;
    size_t shadowCastingLightCount() const;
    size_t forEachShadowCastingLight(std::function<void(size_t, Light&)>);
    size_t forEachShadowCastingLight(std::function<void(size_t, const Light&)>) const;

    // RenderPipelineNode interface

    std::string name() const override { return "Scene"; }
    RenderPipelineNode::ExecuteCallback construct(GpuScene&, Registry&) override;

    // GPU data registration

    void registerLight(SpotLight&);
    void registerLight(DirectionalLight&);
    // TODO: Unregister light!

    // TODO: Replace with something like "registerInstance" which takes a Model and a transform.. or something like that
    void registerMesh(Mesh&);

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

    DrawCallDescription fitVertexAndIndexDataForMesh(Badge<Mesh>, const Mesh&, const VertexLayout&, std::optional<DrawCallDescription> alignWith = {});

    Buffer& globalVertexBufferForLayout(const VertexLayout&) const;
    Buffer& globalIndexBuffer() const;
    IndexType globalIndexBufferType() const;

    BindingSet& globalMaterialBindingSet() const;

    TopLevelAS& globalTopLevelAccelerationStructure() const;

    // Misc.

    void drawGui(bool includeContainingWindow);

private:
    Scene& m_scene;
    Backend& m_backend;

    bool m_maintainRayTracingScene { false };
    // NOTE: It's possible some RT pass would want more vertex info than this, but in all cases I can think of
    // we want either these and nothing more, or nothing at all (e.g. ray traced AO). Remember that vertex positions
    // are always available more directly, as we know our hit point.
    const VertexLayout m_rayTracingVertexLayout = { VertexComponent::Normal3F,
                                                    VertexComponent::TexCoord2F };

    RTGeometryInstance createRTGeometryInstance(Mesh&, uint32_t meshIdx);

    std::unique_ptr<Texture> createShadowMap(const Light&);

    float m_lightPreExposure { 1.0f };

    // GPU data

    std::unique_ptr<Buffer> m_global32BitIndexBuffer { nullptr };
    uint32_t m_nextFreeIndex { 0 };

    std::unordered_map<VertexLayout, std::unique_ptr<Buffer>> m_globalVertexBuffers {};
    uint32_t m_nextFreeVertexIndex { 0 };

    std::vector<Mesh*> m_managedMeshes {};
    std::vector<ShaderDrawable> m_rasterizerMeshData {}; // TODO: Rename to something like m_drawInstances and the type ShaderDrawInstance? Something like that :^)
    std::vector<RTTriangleMesh> m_rayTracingMeshData {};
    static constexpr int MaxSupportedSceneMeshes = 10'000;

    struct ManagedDirectionalLight {
        DirectionalLight* light {};
        TextureHandle shadowMapTex {};
    };
    std::vector<ManagedDirectionalLight> m_managedDirectionalLights {};

    struct ManagedSpotLight {
        SpotLight* light {};
        TextureHandle iesLut {};
        TextureHandle shadowMapTex {};
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
        ShaderMaterial material {};
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

    static constexpr uint32_t InitialMaxRayTracingGeometryInstanceCount { 1024 };
    std::vector<RTGeometryInstance> m_rayTracingGeometryInstances {};
    std::vector<std::unique_ptr<BottomLevelAS>> m_sceneBottomLevelAccelerationStructures {};
    std::unique_ptr<TopLevelAS> m_sceneTopLevelAccelerationStructure {};
    uint32_t m_framesUntilNextFullTlasBuild { 0u };

    std::unique_ptr<Texture> m_environmentMapTexture {};

    // Common textures that can be used for various purposes
    std::unique_ptr<Texture> m_blackTexture {};
    std::unique_ptr<Texture> m_lightGrayTexture {};
    std::unique_ptr<Texture> m_magentaTexture {};
    std::unique_ptr<Texture> m_normalMapBlueTexture {};
};
