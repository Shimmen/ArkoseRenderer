#pragma once

#include "rendering/RenderPipelineNode.h"

class RTFirstHitNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "RT first-hit"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;
};
