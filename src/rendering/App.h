#pragma once

#include "Registry.h"
#include "RenderGraph.h"

class App {
public:
    App() = default;
    virtual ~App() = default;

    virtual std::vector<Backend::Capability> requiredCapabilities() = 0;
    virtual std::vector<Backend::Capability> optionalCapabilities() = 0;

    virtual void setup(RenderGraph&) = 0;
    virtual void update(float elapsedTime, float deltaTime) = 0;
};
