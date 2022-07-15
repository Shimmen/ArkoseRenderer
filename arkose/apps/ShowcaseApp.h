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

    FpsCameraController m_fpsCameraController {};
};
