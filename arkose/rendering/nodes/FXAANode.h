#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

class FXAANode final : public RenderPipelineNode {
public:
    std::string name() const override { return "FXAA"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;
};
