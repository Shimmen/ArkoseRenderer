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
    static std::unique_ptr<Scene> loadFromFile(const std::string&);

    Scene(std::string);
    ~Scene();

    Model* addModel(std::unique_ptr<Model>);

    [[nodiscard]] size_t modelCount() const;
    const Model* operator[](size_t index) const;

    void forEachModel(std::function<void(size_t, const Model&)> callback) const;
    int forEachDrawable(std::function<void(int, const Mesh&)> callback) const;

    void cameraGui();
    const FpsCamera& camera() const { return m_currentMainCamera; }
    FpsCamera& camera() { return m_currentMainCamera; }

    const SunLight& sun() const { return m_sunLight; }
    SunLight& sun() { return m_sunLight; }

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
};
