#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/GpuScene.h"

class GIComposeNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "GI Compose"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;
};
