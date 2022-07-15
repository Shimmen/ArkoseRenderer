#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"
#include "rendering/scene/Model.h"

class ForwardRenderNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "Forward"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    VertexLayout m_vertexLayout { VertexComponent::Position3F,
                                  VertexComponent::TexCoord2F,
                                  VertexComponent::Normal3F,
                                  VertexComponent::Tangent4F };

    enum class ForwardPass {
        Opaque,
        Masked,
    };

    RenderTarget& makeRenderTarget(Registry&, LoadOp) const;
    RenderState& makeRenderState(Registry&, const GpuScene&, ForwardPass) const;
};
