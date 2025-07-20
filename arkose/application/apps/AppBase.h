#pragma once

#include "application/apps/App.h"

class AppBase : public App {
public:
    void setup(Backend&, PhysicsBackend*) override;
    bool update(float elapsedTime, float deltaTime) override;
    void render(Backend&, float elapsedTime, float deltaTime) override;

    Scene& scene() { return *m_scene; }
    RenderPipeline& mainRenderPipeline() override { return *m_renderPipeline; }

protected:
    std::unique_ptr<Scene> m_scene { nullptr };
    std::unique_ptr<RenderPipeline> m_renderPipeline { nullptr };
};
