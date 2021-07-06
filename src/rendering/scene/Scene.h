#pragma once

#include "Model.h"
#include "rendering/scene/lights/DirectionalLight.h"
#include "rendering/scene/lights/SpotLight.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/ProbeGrid.h"
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <unordered_map>

// Shared shader data
using uint = uint32_t;
#include "SceneData.h"

class SceneNode;

class Scene final {
public:
    explicit Scene(Registry&);

    Registry& registry() { return m_registry; }
    const Registry& registry() const { return m_registry; }

    void loadFromFile(const std::string&);

    void update(float elapsedTime, float deltaTime);

    // Camera & view

    const FpsCamera& camera() const { return m_currentMainCamera; }
    FpsCamera& camera() { return m_currentMainCamera; }

    Extent2D mainViewportSize() const { return m_mainViewportSize; }
    void setMainViewportSize(Badge<Backend>, Extent2D size) { m_mainViewportSize = size; }

    float lightPreExposureValue() const { return m_lightPreExposure; }
    void setLightPreExposureValue(Badge<SceneNode>, float value) { m_lightPreExposure = value; }

    void setNextFrameExposureResultBuffer(const Buffer& buffer) { m_nextFrameExposureResultBuffer = &buffer; }
    bool isNextFrameExposureResultBufferReady(Badge<SceneNode>) const;
    const Buffer* popNextFrameExposureResultBuffer(Badge<SceneNode>);

    // Models & meshes

    Model& addModel(std::unique_ptr<Model>);

    size_t modelCount() const { return m_models.size(); }
    size_t meshCount() const;

    void forEachModel(std::function<void(size_t, const Model&)> callback) const;
    void forEachModel(std::function<void(size_t, Model&)> callback);

    int forEachMesh(std::function<void(size_t, const Mesh&)> callback) const;
    int forEachMesh(std::function<void(size_t, Mesh&)> callback);

    // Ray tracing

    bool doesMaintainRayTracingScene() const { return m_maintainRayTracingScene; }
    void setShouldMaintainRayTracingScene(bool);

    // Lighting - direct & indirect

    DirectionalLight& addLight(std::unique_ptr<DirectionalLight>);
    SpotLight& addLight(std::unique_ptr<SpotLight>);

    int forEachLight(std::function<void(size_t, const Light&)>) const;
    int forEachLight(std::function<void(size_t, Light&)>);

    int forEachShadowCastingLight(std::function<void(size_t, const Light&)>) const;
    int forEachShadowCastingLight(std::function<void(size_t, Light&)>);

    const DirectionalLight& sun() const { return *m_directionalLights[0]; }
    DirectionalLight& sun() { return *m_directionalLights[0]; }

    bool hasProbeGrid() { return m_probeGrid.has_value(); }
    void setProbeGrid(ProbeGrid probeGrid) { m_probeGrid = probeGrid; }
    void generateProbeGridFromBoundingBox();
    ProbeGrid& probeGrid() { return m_probeGrid.value(); }

    float& ambient() { return m_ambientIlluminance; }
    float exposedAmbient() const { return m_ambientIlluminance * lightPreExposureValue(); }

    void setEnvironmentMap(std::string path) { m_environmentMap = std::move(path); }
    const std::string& environmentMap() const { return m_environmentMap; }

    float environmentMultiplier() const { return m_environmentMultiplier; }
    float& environmentMultiplier() { return m_environmentMultiplier; }
    float exposedEnvironmentMultiplier() const { return environmentMultiplier() * lightPreExposureValue(); }

    // Meta

    void setSelectedModel(Model* model) { m_selectedModel = model; }
    Model* selectedModel() { return m_selectedModel; }

    void setSelectedMesh(Mesh* mesh) { m_selectedMesh = mesh; }
    Mesh* selectedMesh() { return m_selectedMesh; }

    // Managed GPU assets

    DrawCallDescription fitVertexAndIndexDataForMesh(Badge<Mesh>, const Mesh&, const VertexLayout&, std::optional<DrawCallDescription> alignWith = {});

    Buffer& globalVertexBufferForLayout(const VertexLayout&) const;
    Buffer& globalIndexBuffer() const;
    IndexType globalIndexBufferType() const;

    BindingSet& globalMaterialBindingSet() const;

    // FIXME: We would like to use globalMaterialBindingSet for ray tracing too, but right now we can't!
    const std::vector<Texture*>& globalMaterialTextureArray() const { return m_usedTextures; }
    Buffer* globalMaterialDataBuffer() const { return m_materialDataBuffer; }

    TopLevelAS& globalTopLevelAccelerationStructure() const;

private:
    std::string m_filePath {};

    Registry& m_registry;

    Extent2D m_mainViewportSize {};
    FpsCamera m_currentMainCamera;
    std::unordered_map<std::string, FpsCamera> m_allCameras {};

    std::vector<std::unique_ptr<Model>> m_models;

    std::vector<std::unique_ptr<DirectionalLight>> m_directionalLights;
    std::vector<std::unique_ptr<SpotLight>> m_spotLights;

    std::optional<ProbeGrid> m_probeGrid;

    bool m_maintainRayTracingScene { false };

    std::string m_environmentMap {};
    float m_environmentMultiplier { 1.0f };

    float m_ambientIlluminance { 0.0f };

    float m_lightPreExposure { 1.0f };
    const Buffer* m_nextFrameExposureResultBuffer { nullptr };

    Model* m_selectedModel { nullptr };
    Mesh* m_selectedMesh { nullptr };

    void exposureGUI(FpsCamera&) const;
    void manualExposureGUI(FpsCamera&) const;
    void automaticExposureGUI(FpsCamera&) const;

    // GPU data

    Buffer* m_global32BitIndexBuffer { nullptr };
    uint32_t m_nextFreeIndex { 0 };

    std::unordered_map<VertexLayout, Buffer*> m_globalVertexBuffers {};
    uint32_t m_nextFreeVertexIndex { 0 };

    std::vector<Texture*> m_usedTextures {};
    std::vector<ShaderMaterial> m_usedMaterials {};

    std::vector<RTGeometryInstance> m_rayTracingGeometryInstances {};
    TopLevelAS* m_sceneTopLevelAccelerationStructure {};

    void rebuildGpuSceneData();
    bool m_sceneDataNeedsRebuild { true };
    Buffer* m_materialDataBuffer { nullptr };

    BindingSet* m_materialBindingSet { nullptr };
};
