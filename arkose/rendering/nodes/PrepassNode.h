#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

enum class PrepassMode {
    OpaqueObjectsOnly,
    AllOpaquePixels,
};

class PrepassNode final : public RenderPipelineNode {
public:

    PrepassNode(PrepassMode);

    std::string name() const override { return "Prepass"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    PrepassMode m_mode;

    enum class PassType {
        Opaque,
        Masked,
    };

    RenderState& makeRenderState(Registry&, GpuScene const&, PassType) const;
};
