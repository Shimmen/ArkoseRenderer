#pragma once

#include "rendering/RenderPipelineNode.h"

class DirectionalLightShadowNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Directional light shadow"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    VertexLayout m_vertexLayout { VertexComponent::Position3F,
                                  VertexComponent::TexCoord2F,
                                  VertexComponent::Normal3F,
                                  VertexComponent::Tangent4F };

    // NOTE: No physical unit to this right now..
    float m_lightDiscRadius = 3.6f;
};
