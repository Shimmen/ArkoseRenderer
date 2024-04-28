#pragma once

#include "rendering/RenderPipelineNode.h"

class PathTracerNode final : public RenderPipelineNode {
public:
    PathTracerNode();

    std::string name() const override { return "Path tracer"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    u32 m_accumulatedFrames { 0 };
};
