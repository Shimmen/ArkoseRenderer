#pragma once

#include "rendering/RenderPipelineNode.h"

class SSAONode final : public RenderPipelineNode {
public:

    std::string name() const override { return "SSAO"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    std::vector<vec4> generateKernel(int numSamples) const;

    float m_kernelRadius { 0.58f };
    float m_kernelExponent { 1.75f };
};
