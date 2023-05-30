#pragma once

#include "asset/LevelAsset.h"
#include "rendering/RenderPipelineNode.h"
#include "rendering/StaticMesh.h"
#include "scene/camera/Camera.h"
#include "scene/EnvironmentMap.h"
#include "scene/ProbeGrid.h"
#include "scene/editor/EditorGizmo.h"
#include "scene/lights/DirectionalLight.h"
#include "scene/lights/SphereLight.h"
#include "scene/lights/SpotLight.h"
#include "scene/MeshInstance.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

class Backend;
class GpuScene;
class LevelAsset;
class PhysicsBackend;
class PhysicsScene;
class SceneNode;
class StaticMeshAsset;

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
        bool meshShadingCapable { true };
    };

    void setupFromDescription(const Description&);

    // Scene variant accessors

    GpuScene& gpuScene() { return *m_gpuScene; }
    const GpuScene& gpuScene() const { return *m_gpuScene; }

    bool hasPhysicsScene() const { return m_physicsScene != nullptr; }
    PhysicsScene& physicsScene() { return *m_physicsScene; }
    const PhysicsScene& physicsScene() const { return *m_physicsScene; }

    // Level

    void addLevel(LevelAsset*);
    //void removeLevel(LevelAsset*);

    // Camera

    Camera& addCamera(const std::string& name, bool makeDefault);

    const Camera& camera() const { ARKOSE_ASSERT(m_currentMainCamera); return *m_currentMainCamera; }
    Camera& camera() { ARKOSE_ASSERT(m_currentMainCamera); return *m_currentMainCamera; }

    // Meshes

    StaticMeshInstance& addMesh(StaticMeshAsset*, Transform = Transform());
    StaticMeshInstance& createStaticMeshInstance(StaticMeshHandle, Transform);

    // NOTE: This is more of a utility for now to clear out the current level
    void clearAllMeshInstances();

    // Lighting - direct & indirect

    void addLight(std::unique_ptr<Light>);
    DirectionalLight& addLight(std::unique_ptr<DirectionalLight>);
    SphereLight& addLight(std::unique_ptr<SphereLight>);
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

    void setEnvironmentMap(EnvironmentMap);
    const EnvironmentMap& environmentMap() const { return m_environmentMap; }

    // Meta

    void clearSelectedObject();
    void setSelectedObject(IEditorObject&);
    void setSelectedObject(Light& light);
    void setSelectedObject(StaticMeshInstance& meshInstance);
    IEditorObject* selectedObject() { return m_selectedObject; }

    EditorGizmo* raycastScreenPointAgainstEditorGizmos(vec2 screenPoint);

    // GUI

    void drawSettingsGui(bool includeContainingWindow = false);
    void drawInstanceBoundingBox(StaticMeshInstance const&);
    void drawSceneGizmos();

private:
    Description m_description {};

    // Manages all GPU & render specific data of this scene
    std::unique_ptr<GpuScene> m_gpuScene {};
    // Manages all physics & collision for this scene
    std::unique_ptr<PhysicsScene> m_physicsScene {};

    Camera* m_currentMainCamera { nullptr };
    std::unordered_map<std::string, std::unique_ptr<Camera>> m_allCameras {};

    std::vector<std::unique_ptr<DirectionalLight>> m_directionalLights {};
    std::vector<std::unique_ptr<SphereLight>> m_sphereLights {};
    std::vector<std::unique_ptr<SpotLight>> m_spotLights {};

    EnvironmentMap m_environmentMap { .brightnessFactor = 2500.0f };
    float m_ambientIlluminance { 0.0f };

    std::optional<ProbeGrid> m_probeGrid {};

    IEditorObject* m_selectedObject { nullptr };

    bool m_shouldDrawAllInstanceBoundingBoxes { false };
    bool m_shouldDrawSelectedInstanceBoundingBox { false };

    bool m_shouldDrawGizmos { false };
    std::vector<EditorGizmo> m_editorGizmos {};

};
