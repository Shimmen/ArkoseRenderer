#pragma once

#include "rendering/Registry.h"
#include "rendering/RenderPipeline.h"
#include "scene/Scene.h"

class App {
public:
    App() = default;
    virtual ~App() = default;

    virtual std::vector<Backend::Capability> requiredCapabilities() { return {}; }
    virtual std::vector<Backend::Capability> optionalCapabilities() { return {}; }

    virtual void setup(Backend& graphicsBackend, PhysicsBackend* physicsBackend) = 0;
    virtual bool update(float elapsedTime, float deltaTime) { return true; }
    virtual void render(Backend&, float elapsedTime, float deltaTime) = 0;

    virtual RenderPipeline& mainRenderPipeline() = 0;
};
