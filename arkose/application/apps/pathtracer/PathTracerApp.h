#pragma once

#include "application/apps/App.h"
#include "scene/camera/FpsCameraController.h"

class PathTracerApp : public App {
public:
    std::vector<Backend::Capability> requiredCapabilities() override;
    void setup(Scene&, RenderPipeline&) override;
    bool update(Scene&, float elapsedTime, float deltaTime) override;

    bool m_guiEnabled { true };
    RenderPipeline* m_renderPipeline { nullptr };
    FpsCameraController m_fpsCameraController {};
};
