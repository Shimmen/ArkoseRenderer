#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/GpuScene.h"

class SSAONode final : public RenderPipelineNode {
public:

    std::string name() const override { return "SSAO"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    std::vector<vec4> generateKernel(int numSamples) const;
};
