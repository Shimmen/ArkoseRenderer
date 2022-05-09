#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"

class RTFirstHitNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "RT first-hit"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;
};
