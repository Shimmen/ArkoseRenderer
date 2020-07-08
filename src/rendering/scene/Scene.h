#pragma once

#include "Model.h"
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>

class SunLight {
public:
    mat4 lightProjection() const;

    vec3 color;
    float intensity;

    vec3 direction;
    Extent2D shadowMapSize;
    float worldExtent;
};

class Scene {
public:
    static constexpr const char* savedCamerasFile = "assets/cameras.json";

    Scene() = default;
    ~Scene();

    void loadFromFile(const std::string&);

    Model* addModel(std::unique_ptr<Model>);

    size_t modelCount() const { return m_models.size(); }
    size_t meshCount() const;

    void forEachModel(std::function<void(size_t, const Model&)> callback) const;
    int forEachMesh(std::function<void(size_t, const Mesh&)> callback) const;

    const FpsCamera& camera() const { return m_currentMainCamera; }
    FpsCamera& camera() { return m_currentMainCamera; }
    void cameraGui();

    const SunLight& sun() const { return m_sunLight; }
    SunLight& sun() { return m_sunLight; }

    float& ambient() { return m_ambient; }

    void setEnvironmentMap(std::string path) { m_environmentMap = std::move(path); }
    const std::string& environmentMap() const { return m_environmentMap; }

    float environmentMultiplier() const { return m_environmentMultiplier; }
    float& environmentMultiplier() { return m_environmentMultiplier; }

private:
    void loadAdditionalCameras();
    static std::unique_ptr<Model> loadProxy(const std::string&);

private:
    std::string m_loadedPath {};

    std::vector<std::unique_ptr<Model>> m_models;
    SunLight m_sunLight;

    FpsCamera m_currentMainCamera;
    std::unordered_map<std::string, FpsCamera> m_allCameras {};

    std::string m_environmentMap {};
    float m_environmentMultiplier { 1.0f };

    float m_ambient { 0.0f };
};
