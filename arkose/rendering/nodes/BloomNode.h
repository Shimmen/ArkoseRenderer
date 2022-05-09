#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/GpuScene.h"

class BloomNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Bloom"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;
};
