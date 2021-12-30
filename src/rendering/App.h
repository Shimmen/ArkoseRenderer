#pragma once

#include "Registry.h"
#include "RenderPipeline.h"
#include "rendering/scene/Scene.h"

class App {
public:
    App() = default;
    virtual ~App() = default;

    virtual std::vector<Backend::Capability> requiredCapabilities() { return {}; };
    virtual std::vector<Backend::Capability> optionalCapabilities() { return {}; };

    virtual void setup(Scene&, RenderGraph&) = 0;
    virtual void update(Scene&, float elapsedTime, float deltaTime) {};
};
