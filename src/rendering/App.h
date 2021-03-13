#pragma once

#include "Registry.h"
#include "RenderGraph.h"
#include "rendering/scene/Scene.h"

class App {
public:
    App() = default;
    virtual ~App() = default;

    virtual std::vector<Backend::Capability> requiredCapabilities() = 0;
    virtual std::vector<Backend::Capability> optionalCapabilities() = 0;

    virtual void setup(Scene&, RenderGraph&) = 0;
    virtual void update(Scene&, float elapsedTime, float deltaTime) = 0;
};
