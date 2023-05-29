#pragma once

#include "apps/App.h"
#include "scene/camera/FpsCameraController.h"

class ShowcaseApp : public App {
public:
    std::vector<Backend::Capability> requiredCapabilities();
    void setup(Scene&, RenderPipeline&) override;
    bool update(Scene&, float elapsedTime, float deltaTime) override;

    bool drawGui(Scene&);

    enum class AntiAliasing {
        None,
        TAA,
        FXAA,
    };

    bool m_guiEnabled { true };
    RenderPipeline* m_renderPipeline { nullptr };
    FpsCameraController m_fpsCameraController {};

    // Demo scene
    void setupCullingShowcaseScene(Scene&);
    struct AnimatingInstance {
        StaticMeshInstance* staticMeshInstance {};
        vec3 axisOfRotation {};
        float rotationSpeed { 1.0f };
    };
    std::vector<AnimatingInstance> m_animatingInstances {};
};
