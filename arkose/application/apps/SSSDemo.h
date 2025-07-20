#pragma once

#include "application/apps/AppBase.h"
#include "scene/camera/FpsCameraController.h"

class SSSDemo : public AppBase {
public:
    std::vector<Backend::Capability> requiredCapabilities() override;

    void setup(Backend& graphicsBackend, PhysicsBackend* physicsBackend) override;
    bool update(float elapsedTime, float deltaTime) override;
    void render(Backend&, float elapsedTime, float deltaTime) override;

    bool m_guiEnabled { true };
    FpsCameraController m_cameraController {};
};
