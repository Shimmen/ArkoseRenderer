#pragma once

#include "application/apps/App.h"

class AppBase : public App {
public:
    void setup(Backend&, PhysicsBackend*) override;
    bool update(float elapsedTime, float deltaTime) override;
    void render(Backend&, float elapsedTime, float deltaTime) override;

    Scene& mainScene() { return *m_mainScene; }
    RenderPipeline& mainRenderPipeline() override { return *m_mainRenderPipeline; }

private:
    std::unique_ptr<Scene> m_mainScene { nullptr };
    std::unique_ptr<RenderPipeline> m_mainRenderPipeline { nullptr };
};
