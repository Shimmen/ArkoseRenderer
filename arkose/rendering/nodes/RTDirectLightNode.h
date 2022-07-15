#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/GpuScene.h"

class RTDirectLightNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "RT direct light"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;
};
