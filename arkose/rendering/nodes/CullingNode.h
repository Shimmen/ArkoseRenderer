#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

class CullingNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Culling"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    // TODO: We currently have some problems with this culling node, so I'm turning off culling for now.
    // Soon enough we will deprecate and remove this culling node anyway.
    bool m_frustumCull { false };
};
