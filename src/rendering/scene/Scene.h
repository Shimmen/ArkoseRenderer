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

class Scene final {
public:
    static constexpr const char* savedCamerasFile = "assets/cameras.json";

    explicit Scene(Registry&);
    ~Scene();

    Registry& registry() { return m_registry; }
    const Registry& registry() const { return m_registry; }

    void loadFromFile(const std::string&);

    Model& addModel(std::unique_ptr<Model>);
    DirectionalLight& addLight(std::unique_ptr<DirectionalLight>);
    SpotLight& addLight(std::unique_ptr<SpotLight>);

    size_t modelCount() const { return m_models.size(); }
    size_t meshCount() const;

    void forEachModel(std::function<void(size_t, const Model&)> callback) const;
    void forEachModel(std::function<void(size_t, Model&)> callback);

    int forEachMesh(std::function<void(size_t, const Mesh&)> callback) const;
    int forEachMesh(std::function<void(size_t, Mesh&)> callback);

    void setSelectedModel(Model* model) { m_selectedModel = model; }
    Model* selectedModel() { return m_selectedModel; }

    void setSelectedMesh(Mesh* mesh) { m_selectedMesh = mesh; }
    Mesh* selectedMesh() { return m_selectedMesh; }

    const FpsCamera& camera() const { return m_currentMainCamera; }
    FpsCamera& camera() { return m_currentMainCamera; }
    void cameraGui();

    const DirectionalLight& sun() const { return *m_directionalLights[0]; }
    DirectionalLight& sun() { return *m_directionalLights[0]; }

    int forEachLight(std::function<void(size_t, const Light&)>) const;
    int forEachLight(std::function<void(size_t, Light&)>);

    bool hasProbeGrid() { return m_probeGrid.has_value(); }
    void setProbeGrid(ProbeGrid probeGrid) { m_probeGrid = probeGrid; }
    void generateProbeGridFromBoundingBox();
    ProbeGrid& probeGrid() { return m_probeGrid.value(); }

    float& ambient() { return m_ambientIlluminance; }

    void setEnvironmentMap(std::string path) { m_environmentMap = std::move(path); }
    const std::string& environmentMap() const { return m_environmentMap; }

    float environmentMultiplier() const { return m_environmentMultiplier; }
    float& environmentMultiplier() { return m_environmentMultiplier; }

private:
    void loadAdditionalCameras();
    static std::unique_ptr<Model> loadProxy(const std::string&);

private:
    std::string m_loadedPath {};

    Registry& m_registry;

    std::vector<std::unique_ptr<Model>> m_models;

    std::vector<std::unique_ptr<DirectionalLight>> m_directionalLights;
    std::vector<std::unique_ptr<SpotLight>> m_spotLights;

    std::optional<ProbeGrid> m_probeGrid;

    FpsCamera m_currentMainCamera;
    std::unordered_map<std::string, FpsCamera> m_allCameras {};

    std::string m_environmentMap {};
    float m_environmentMultiplier { 1.0f };

    float m_ambientIlluminance { 0.0f };

    Model* m_selectedModel { nullptr };
    Mesh* m_selectedMesh { nullptr };
};
