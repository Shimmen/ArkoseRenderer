#pragma once

#include "rendering/RenderPipelineNode.h"

class ForwardRenderNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "Forward"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    enum class ForwardPass {
        Opaque,
        Masked,
    };

    RenderTarget& makeRenderTarget(Registry&, LoadOp) const;
    RenderState& makeRenderState(Registry&, const GpuScene&, ForwardPass) const;
};
