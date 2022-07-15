#pragma once

#include "rendering/Registry.h"
#include "rendering/RenderPipeline.h"
#include "rendering/scene/Scene.h"

class App {
public:
    App() = default;
    virtual ~App() = default;

    virtual std::vector<Backend::Capability> requiredCapabilities() { return {}; };
    virtual std::vector<Backend::Capability> optionalCapabilities() { return {}; };

    virtual void setup(Scene&, RenderPipeline&) = 0;
    virtual bool update(Scene&, float elapsedTime, float deltaTime) { return true; };
};
