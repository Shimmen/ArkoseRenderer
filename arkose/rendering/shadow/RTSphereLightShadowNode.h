#pragma once

#include "rendering/RenderPipelineNode.h"

class RTSphereLightShadowNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "RT Sphere light shadow"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;
};
