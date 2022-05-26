#pragma once

#include "rendering/RenderPipelineNode.h"

class DepthOfFieldNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Depth of Field"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;
};
