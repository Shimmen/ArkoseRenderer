#pragma once

#include "rendering/RenderPipelineNode.h"

class DirectionalLightShadowNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Directional light shadow"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    VertexLayout m_vertexLayout { VertexComponent::Position3F,
                                  VertexComponent::Normal3F };

    // NOTE: No physical unit to this right now..
    float m_lightDiscRadius = 3.6f;
};
