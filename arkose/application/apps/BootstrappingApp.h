#pragma once

#include "application/apps/AppBase.h"

class BootstrappingApp : public AppBase {
public:
    void setup(Backend& graphicsBackend, PhysicsBackend* physicsBackend) override;
    bool update(float elapsedTime, float deltaTime) override;
    void render(Backend&, float elapsedTime, float deltaTime) override;
};
