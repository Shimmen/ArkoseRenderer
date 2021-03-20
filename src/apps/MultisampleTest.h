#pragma once

#include "rendering/App.h"
#include "rendering/scene/Scene.h"

class MultisampleTest : public App {
public:
    void setup(Scene&, RenderGraph&) override;
};
