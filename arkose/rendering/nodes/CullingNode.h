#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

class CullingNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Culling"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;
};
