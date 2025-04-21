#pragma once

#include "rendering/RenderPipelineNode.h"

class DirectionalShadowProjectNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Directional shadow project"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    // NOTE: No physical unit to this right now..
    float m_lightDiscRadius = 2.4f;
    Texture* m_projectedShadow { nullptr };
};
