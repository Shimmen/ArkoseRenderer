#pragma once

#include "rendering/App.h"
#include "rendering/scene/Scene.h"
#include "rendering/camera/FpsCameraController.h"

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
