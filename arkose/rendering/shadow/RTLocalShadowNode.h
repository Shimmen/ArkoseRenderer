#pragma once

#include "rendering/RenderPipelineNode.h"

class Texture;

class RTLocalShadowNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "RT local light shadow"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    GpuScene* m_scene { nullptr };
    Texture* m_shadowTex { nullptr };
};
