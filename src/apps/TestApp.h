#pragma once

#include "rendering/App.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class TestApp : public App {
public:
    std::vector<Backend::Capability> requiredCapabilities() override;
    std::vector<Backend::Capability> optionalCapabilities() override;

    void setup(RenderGraph&) override;
    void update(float elapsedTime, float deltaTime) override;

private:
    std::unique_ptr<Scene> m_scene {};
};
