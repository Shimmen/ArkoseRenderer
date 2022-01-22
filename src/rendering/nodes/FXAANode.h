#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class FXAANode final : public RenderPipelineNode {
public:
    std::string name() const override { return "FXAA"; }
    ExecuteCallback construct(Scene&, Registry&) override;
};
