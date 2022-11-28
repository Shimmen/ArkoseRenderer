#pragma once

#include "rendering/RenderPipelineNode.h"

class MeshletDebugNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Meshlet Debug"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
};
