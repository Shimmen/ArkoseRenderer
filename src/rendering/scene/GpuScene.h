#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/camera/Camera.h"
#include "rendering/scene/Scene.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

// Shared shader data
using uint = uint32_t;
#include "SceneData.h"

class DirectionalLight;
class Model;
class SpotLight;

class GpuScene final : public RenderPipelineNode {
public:
    GpuScene(Scene&, Extent2D initialMainViewportSize);

    Scene& scene() { return m_scene; }
    const Scene& scene() const { return m_scene; }

    Camera& camera() { return scene().camera(); }
    const Camera& camera() const { return scene().camera(); }

    // RenderPipelineNode interface

    std::string name() const override { return "Scene"; }
    RenderPipelineNode::ExecuteCallback construct(GpuScene&, Registry&) override;

    // GPU data registration

    void registerModel(Model&);
    void registerLight(SpotLight&);
    void registerLight(DirectionalLight&);

    // Ray tracing

    bool doesMaintainRayTracingScene() const { return m_maintainRayTracingScene; }
    void setShouldMaintainRayTracingScene(Badge<Scene>, bool);

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

    void drawGui();

private:
    Scene& m_scene;

    std::vector<Model*> m_models {};

    std::vector<DirectionalLight*> m_directionalLights {};
    std::vector<SpotLight*> m_spotLights {};

    bool m_maintainRayTracingScene { false };
    // NOTE: It's possible some RT pass would want more vertex info than this, but in all cases I can think of
    // we want either these and nothing more, or nothing at all (e.g. ray traced AO). Remember that vertex positions
    // are always available more directly, as we know our hit point.
    const VertexLayout m_rayTracingVertexLayout = { VertexComponent::Normal3F,
                                                    VertexComponent::TexCoord2F };

    float m_lightPreExposure { 1.0f };

    // GPU data

    std::unique_ptr<Buffer> m_global32BitIndexBuffer { nullptr };
    uint32_t m_nextFreeIndex { 0 };

    std::unordered_map<VertexLayout, std::unique_ptr<Buffer>> m_globalVertexBuffers {};
    uint32_t m_nextFreeVertexIndex { 0 };

    std::vector<Texture*> m_usedTextures {};
    std::vector<ShaderMaterial> m_usedMaterials {};

    static constexpr uint32_t InitialMaxRayTracingGeometryInstanceCount { 1024 };
    std::vector<RTGeometryInstance> m_rayTracingGeometryInstances {};
    std::vector<std::unique_ptr<BottomLevelAS>> m_sceneBottomLevelAccelerationStructures {};
    std::unique_ptr<TopLevelAS> m_sceneTopLevelAccelerationStructure {};

    std::unique_ptr<Texture> m_environmentMapTexture {};

public:
    // TODO: while refactoring keep this visible (for the Scene)
    void rebuildGpuSceneData();
private:

    bool m_sceneDataNeedsRebuild { true };
    std::unique_ptr<Buffer> m_materialDataBuffer { nullptr };

    std::unique_ptr<BindingSet> m_materialBindingSet { nullptr };
};
