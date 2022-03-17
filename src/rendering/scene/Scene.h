#pragma once

#include "Model.h"
#include "rendering/RenderPipelineNode.h"
#include "rendering/camera/Camera.h"
#include "rendering/scene/ProbeGrid.h"
#include "rendering/scene/lights/DirectionalLight.h"
#include "rendering/scene/lights/SpotLight.h"
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <unordered_map>

class SceneNode;
class GpuScene;

class Scene final {
public:
    Scene(Extent2D initialMainViewportSize);
    ~Scene();

    void newFrame(Extent2D mainViewportSize, bool firstFrame);
    void update(float elapsedTime, float deltaTime);

    struct Description {
        std::string path {};
        bool maintainRayTracingScene { true };
    };

    void setupFromDescription(const Description&);

    // Scene version accessors

    GpuScene& gpuScene() { return *m_gpuScene; }
    //PhysicScene& physicsScene() { return m_physicsScene; }

    // Camera & view

    const Camera& camera() const { return *m_currentMainCamera; }
    Camera& camera() { return *m_currentMainCamera; }

    // Models & meshes

    Model& addModel(std::unique_ptr<Model>);

    size_t modelCount() const { return m_models.size(); }
    size_t meshCount() const;

    void forEachModel(std::function<void(size_t, const Model&)> callback) const;
    void forEachModel(std::function<void(size_t, Model&)> callback);

    int forEachMesh(std::function<void(size_t, const Mesh&)> callback) const;
    int forEachMesh(std::function<void(size_t, Mesh&)> callback);

    // Lighting - direct & indirect

    DirectionalLight& addLight(std::unique_ptr<DirectionalLight>);
    SpotLight& addLight(std::unique_ptr<SpotLight>);

    size_t spotLightCount() const { return m_spotLights.size(); }
    size_t directionalLightCount() const { return m_directionalLights.size(); }

    DirectionalLight* firstDirectionalLight();

    int forEachLight(std::function<void(size_t, const Light&)>) const;
    int forEachLight(std::function<void(size_t, Light&)>);

    int forEachShadowCastingLight(std::function<void(size_t, const Light&)>) const;
    int forEachShadowCastingLight(std::function<void(size_t, Light&)>);

    void setAmbientIlluminance(float illuminance) { m_ambientIlluminance = illuminance; }
    float ambientIlluminance() const { return m_ambientIlluminance; }
    
    bool hasProbeGrid() { return m_probeGrid.has_value(); }
    void setProbeGrid(ProbeGrid probeGrid) { m_probeGrid = probeGrid; }
    void generateProbeGridFromBoundingBox();
    ProbeGrid& probeGrid() { return m_probeGrid.value(); }

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

    private:

    // Serialization

    void loadFromFile(const std::string&);

    // GUI

    void drawSceneGui();
    void drawSceneGizmos();

private:
    Description m_description {};

    // Manages all GPU & render specific data of this scene
    std::unique_ptr<GpuScene> m_gpuScene {};

    Camera* m_currentMainCamera {};
    std::unordered_map<std::string, std::unique_ptr<Camera>> m_allCameras {};

    std::vector<std::unique_ptr<Model>> m_models {};
    
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
