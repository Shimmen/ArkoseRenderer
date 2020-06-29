#pragma once

#include "rendering/App.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class TestApp : public App {
public:
    void setup(RenderGraph&) override;
    void update(float elapsedTime, float deltaTime) override;

private:
    std::unique_ptr<Scene> m_scene {};
};
