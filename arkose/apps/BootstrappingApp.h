#pragma once

#include "rendering/App.h"
#include "rendering/scene/Scene.h"

class BootstrappingApp : public App {
public:
    void setup(Scene&, RenderPipeline&) override;
    bool update(Scene&, float elapsedTime, float deltaTime) override;
};
