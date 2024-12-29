#pragma once

#include "application/apps/App.h"
#include "scene/camera/FpsCameraController.h"

// For animation & skinning tests
#include "animation/Animation.h"

class ShowcaseApp : public App {
public:
    std::vector<Backend::Capability> requiredCapabilities() override;
    void setup(Scene&, RenderPipeline&) override;
    bool update(Scene&, float elapsedTime, float deltaTime) override;

    bool drawGui(Scene&);

    enum class AntiAliasing {
        None,
        TAA,
    };

    bool m_guiEnabled { true };
    RenderPipeline* m_renderPipeline { nullptr };
    FpsCameraController m_fpsCameraController {};

    // TODO: Remove me, only for testing skeletal mesh animations 
    // TODO: In fact, we should make this into a test at some point..
    SkeletalMeshInstance* m_skeletalMeshInstance { nullptr };
    std::unique_ptr<Animation> m_testAnimation { nullptr };

    // Demo scene
    void setupCullingShowcaseScene(Scene&);
    struct AnimatingInstance {
        StaticMeshInstance* staticMeshInstance {};
        vec3 axisOfRotation {};
        float rotationSpeed { 1.0f };
    };
    std::vector<AnimatingInstance> m_animatingInstances {};
};
