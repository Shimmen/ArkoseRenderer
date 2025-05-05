#pragma once

#include "asset/LevelAsset.h"
#include "rendering/ResourceList.h"
#include "rendering/StaticMesh.h"
#include "scene/camera/Camera.h"
#include "scene/EnvironmentMap.h"
#include "scene/ProbeGrid.h"
#include "scene/lights/DirectionalLight.h"
#include "scene/lights/SphereLight.h"
#include "scene/lights/SpotLight.h"
#include "scene/MeshInstance.h"
#include "scene/SceneNode.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

class Backend;
class CubeLUT;
class EditorScene;
class GpuScene;
class LevelAsset;
class MeshAsset;
class SetAsset;
class NodeAsset;
class PhysicsBackend;
class PhysicsScene;
class SceneNode;

class Scene final {
public:
    Scene(Backend&, PhysicsBackend*);
    ~Scene();

    void update(float elapsedTime, float deltaTime);

    void preRender();
    void postRender();

    struct Description {
        std::string path {};
        bool createEditorScene { true };
        bool withRayTracing { false };
        bool withMeshShading { false };
    };

    void setupFromDescription(const Description&);

    // Scene variant accessors

    GpuScene& gpuScene() { return *m_gpuScene; }
    const GpuScene& gpuScene() const { return *m_gpuScene; }

    bool hasEditorScene() const { return m_editorScene != nullptr; }
    EditorScene& editorScene() { return *m_editorScene; }

    bool hasPhysicsScene() const { return m_physicsScene != nullptr; }
    PhysicsScene& physicsScene() { return *m_physicsScene; }
    const PhysicsScene& physicsScene() const { return *m_physicsScene; }

    // Level & set

    void addLevel(LevelAsset*);
    //void removeLevel(LevelAsset*);

    SceneNodeHandle rootNode() const { return m_rootNode; }
    SceneNode* node(SceneNodeHandle handle) { return handle.valid() ? &m_sceneNodes.get(handle) : nullptr; }

    SceneNodeHandle addNode(Transform const&, std::string_view name, SceneNodeHandle parent);
    void removeNode(SceneNodeHandle);

    SceneNodeHandle addSet(SetAsset*);
    SceneNodeHandle addSet(SetAsset*, SceneNodeHandle parent);

    // Camera

    Camera& addCamera(const std::string& name, bool makeDefault);

    const Camera& camera() const { ARKOSE_ASSERT(m_currentMainCamera); return *m_currentMainCamera; }
    Camera& camera() { ARKOSE_ASSERT(m_currentMainCamera); return *m_currentMainCamera; }

    // Meshes

    SkeletalMeshInstance& addSkeletalMesh(MeshAsset*, SkeletonAsset*, Transform = Transform());
    SkeletalMeshInstance& createSkeletalMeshInstance(SkeletalMeshHandle, Transform);

    StaticMeshInstance& addMesh(MeshAsset*, Transform = Transform());
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

    void setColorGradingLUT(CubeLUT const*);

    // GUI

    void drawSettingsGui(bool includeContainingWindow = false);

private:
    Description m_description {};

    // Manages all GPU & render specific data of this scene
    std::unique_ptr<GpuScene> m_gpuScene {};
    // Manages all physics & collision for this scene
    std::unique_ptr<PhysicsScene> m_physicsScene {};
    // Manages all editor specific data & logic of this scene
    std::unique_ptr<EditorScene> m_editorScene {};

    // Scene hierarchy & nodes

    ResourceList<SceneNode, SceneNodeHandle> m_sceneNodes { "Nodes", 65'536 };
    SceneNodeHandle m_rootNode {};

    SceneNodeHandle addNodeRecursive(SetAsset*, NodeAsset*, SceneNodeHandle parent);

    Camera* m_currentMainCamera { nullptr };
    std::unordered_map<std::string, std::unique_ptr<Camera>> m_allCameras {};

    std::vector<std::unique_ptr<DirectionalLight>> m_directionalLights {};
    std::vector<std::unique_ptr<SphereLight>> m_sphereLights {};
    std::vector<std::unique_ptr<SpotLight>> m_spotLights {};

    EnvironmentMap m_environmentMap { .brightnessFactor = 2500.0f };
    float m_ambientIlluminance { 0.0f };

    std::optional<ProbeGrid> m_probeGrid {};

};
