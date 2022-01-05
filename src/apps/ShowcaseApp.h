#pragma once

#include "rendering/App.h"
#include "rendering/scene/Scene.h"

class ShowcaseApp : public App {
public:
    std::vector<Backend::Capability> requiredCapabilities();
    void setup(Scene&, RenderPipeline&) override;
    void update(Scene&, float elapsedTime, float deltaTime) override;

    enum class AntiAliasing {
        None,
        TAA,
        FXAA,
    };
};
