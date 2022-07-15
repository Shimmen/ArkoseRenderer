#pragma once

#include "apps/App.h"

class BootstrappingApp : public App {
public:
    void setup(Scene&, RenderPipeline&) override;
    bool update(Scene&, float elapsedTime, float deltaTime) override;
};
