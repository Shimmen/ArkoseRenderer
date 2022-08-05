#pragma once

#include "Model.h"
#include "rendering/RenderPipelineNode.h"
#include "rendering/StaticMesh.h"
#include "scene/camera/Camera.h"
#include "scene/ProbeGrid.h"
#include "scene/lights/DirectionalLight.h"
#include "scene/lights/SpotLight.h"
#include "scene/loader/GltfLoader.h"
#include "scene/MeshInstance.h"
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <unordered_map>

class Backend;
class SceneNode;
class GpuScene;
class PhysicsBackend;
class PhysicsScene;

class Scene final {
public:
    Scene(Backend&, PhysicsBackend*, Extent2D initialMainViewportSize);
    ~Scene();

    void update(float elapsedTime, float deltaTime);

    void preRender();
    void postRender();

    struct Description {
        std::string path {};
        bool maintainRayTracingScene { true };
    };

    void setupFromDescription(const Description&);

    // Scene variant accessors

    GpuScene& gpuScene() { return *m_gpuScene; }
    const GpuScene& gpuScene() const { return *m_gpuScene; }

    bool hasPhysicsScene() const { return m_physicsScene != nullptr; }
    PhysicsScene& physicsScene() { return *m_physicsScene; }
    const PhysicsScene& physicsScene() const { return *m_physicsScene; }

    // Camera

    const Camera& camera() const { return *m_currentMainCamera; }
    Camera& camera() { return *m_currentMainCamera; }

    // Models // NOTE: Old stuff, to be removed!
    //Model& addModel(std::unique_ptr<Model>);

    // Meshes (TODO: Maybe the Scene shouldn't have any knowledge about meshes, and let the GpuScene handle all mesh logic?)

    StaticMeshInstance addMesh(std::shared_ptr<StaticMesh>, Transform);

    std::vector<StaticMeshInstance>& staticMeshInstances() { return m_staticMeshInstances; }
    const std::vector<StaticMeshInstance>& staticMeshInstances() const { return m_staticMeshInstances; }

    // TODO: Later, also count skeletal meshes here
    uint32_t meshInstanceCount() const { return static_cast<uint32_t>(m_staticMeshInstances.size()); }

    // Lighting - direct & indirect

    DirectionalLight& addLight(std::unique_ptr<DirectionalLight>);
    SpotLight& addLight(std::unique_ptr<SpotLight>);

    size_t spotLightCount() const { return m_spotLights.size(); }
    size_t directionalLightCount() const { return m_directionalLights.size(); }

    DirectionalLight* firstDirectionalLight();

    size_t forEachLight(std::function<void(size_t, const Light&)>) const;
    size_t forEachLight(std::function<void(size_t, Light&)>);

    void setAmbientIlluminance(float illuminance) { m_ambientIlluminance = illuminance; }
    float ambientIlluminance() const { return m_ambientIlluminance; }
    
    bool hasProbeGrid() const { return m_probeGrid.has_value(); }
    void setProbeGrid(ProbeGrid probeGrid) { m_probeGrid = probeGrid; }
    void generateProbeGridFromBoundingBox();
    const ProbeGrid& probeGrid() const { return m_probeGrid.value(); }

    float filmGrainGain() const { return m_fixedFilmGrainGain; }

    struct EnvironmentMap {
        std::string assetPath {};
        float brightnessFactor { 1.0f };
    };

    void setEnvironmentMap(EnvironmentMap&);
    const EnvironmentMap& environmentMap() const { return m_environmentMap; }

    // Meta

    void setSelectedModel(Model* model) { m_selectedModel = model; }
    Model* selectedModel() { return m_selectedModel; }

    void setSelectedMesh(Mesh* mesh) { m_selectedMesh = mesh; }
    Mesh* selectedMesh() { return m_selectedMesh; }

    // GUI

    void drawSettingsGui(bool includeContainingWindow = false);
    void drawSceneGizmos();

private:

    // Serialization

    void loadFromFile(const std::string&);

private:
    Description m_description {};

    // Manages all GPU & render specific data of this scene
    std::unique_ptr<GpuScene> m_gpuScene {};
    // Manages all physics & collision for this scene
    std::unique_ptr<PhysicsScene> m_physicsScene {};

    Camera* m_currentMainCamera {};
    std::unordered_map<std::string, std::unique_ptr<Camera>> m_allCameras {};

    // TODO: Remove me!
    std::vector<std::unique_ptr<Model>> m_models {};
    
    // Various loaders, which needs to be kept in memory as they own their loaded resources until someone takes over
    GltfLoader m_gltfLoader {};

    std::vector<StaticMeshInstance> m_staticMeshInstances {};
    
    std::vector<std::unique_ptr<DirectionalLight>> m_directionalLights {};
    std::vector<std::unique_ptr<SpotLight>> m_spotLights {};

    EnvironmentMap m_environmentMap {};
    float m_ambientIlluminance { 0.0f };

    std::optional<ProbeGrid> m_probeGrid {};

    // TODO: Maybe move to the camera?
    float m_fixedFilmGrainGain { 0.040f };

    Model* m_selectedModel { nullptr };
    Mesh* m_selectedMesh { nullptr };

};
