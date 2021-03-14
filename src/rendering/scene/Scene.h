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
    explicit Scene(Registry&);

    Registry& registry() { return m_registry; }
    const Registry& registry() const { return m_registry; }

    void loadFromFile(const std::string&);

    void manageResources();

    // Camera & view

    const FpsCamera& camera() const { return m_currentMainCamera; }
    FpsCamera& camera() { return m_currentMainCamera; }

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

    int forEachLight(std::function<void(size_t, const Light&)>) const;
    int forEachLight(std::function<void(size_t, Light&)>);

    const DirectionalLight& sun() const { return *m_directionalLights[0]; }
    DirectionalLight& sun() { return *m_directionalLights[0]; }

    bool hasProbeGrid() { return m_probeGrid.has_value(); }
    void setProbeGrid(ProbeGrid probeGrid) { m_probeGrid = probeGrid; }
    void generateProbeGridFromBoundingBox();
    ProbeGrid& probeGrid() { return m_probeGrid.value(); }

    float& ambient() { return m_ambientIlluminance; }

    void setEnvironmentMap(std::string path) { m_environmentMap = std::move(path); }
    const std::string& environmentMap() const { return m_environmentMap; }

    float environmentMultiplier() const { return m_environmentMultiplier; }
    float& environmentMultiplier() { return m_environmentMultiplier; }

    // Meta

    void setSelectedModel(Model* model) { m_selectedModel = model; }
    Model* selectedModel() { return m_selectedModel; }

    void setSelectedMesh(Mesh* mesh) { m_selectedMesh = mesh; }
    Mesh* selectedMesh() { return m_selectedMesh; }

    // Managed GPU assets

    DrawCall fitVertexAndIndexDataForMesh(Badge<Mesh>, const Mesh&, const VertexLayout&);

    Buffer& globalVertexBufferForLayout(const VertexLayout&) const;
    Buffer& globalIndexBuffer() const;
    IndexType globalIndexBufferType() const;

private:
    std::string m_filePath {};

    Registry& m_registry;

    FpsCamera m_currentMainCamera;
    std::unordered_map<std::string, FpsCamera> m_allCameras {};

    std::vector<std::unique_ptr<Model>> m_models;

    std::vector<std::unique_ptr<DirectionalLight>> m_directionalLights;
    std::vector<std::unique_ptr<SpotLight>> m_spotLights;

    std::optional<ProbeGrid> m_probeGrid;

    std::string m_environmentMap {};
    float m_environmentMultiplier { 1.0f };

    float m_ambientIlluminance { 0.0f };

    Model* m_selectedModel { nullptr };
    Mesh* m_selectedMesh { nullptr };

    // GPU data

    static constexpr size_t InitialIndexBufferSize = 25 * 1024 * 1024;
    static constexpr size_t InitialVertexBufferSize = 100 * 1024 * 1024;

    struct ResizableBuffer {
        Buffer* buffer { nullptr };
        size_t offsetToNextFree { 0 };
    };

    ResizableBuffer m_global32BitIndexBuffer {};
    std::unordered_map<VertexLayout, std::unique_ptr<ResizableBuffer>> m_globalVertexBuffers {};
};
