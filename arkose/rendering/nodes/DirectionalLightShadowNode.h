#pragma once

#include "rendering/RenderPipelineNode.h"

class DirectionalLightShadowNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Directional light shadow"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    // NOTE: No physical unit to this right now..
    float m_lightDiscRadius = 3.6f;

    Texture* m_shadowMap { nullptr };
    Texture* m_projectedShadow { nullptr };
};
