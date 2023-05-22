#pragma once

#include "rendering/RenderPipelineNode.h"

class VisibilityBufferShadingNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Visibility buffer shading"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    // ..

};
