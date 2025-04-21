#pragma once

#include "rendering/RenderPipelineNode.h"

class DirectionalLightShadowNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Directional light shadow"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    Texture* m_shadowMap { nullptr };
};
