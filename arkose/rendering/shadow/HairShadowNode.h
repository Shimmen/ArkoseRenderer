#pragma once

#include "rendering/RenderPipelineNode.h"

class HairShadowNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Hair shadows"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    Texture* m_depthMap { nullptr };
};
