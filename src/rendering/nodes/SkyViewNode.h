#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/GpuScene.h"

class SkyViewNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Sky view"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;
};
