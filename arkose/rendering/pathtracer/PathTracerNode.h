#pragma once

#include "rendering/RenderPipelineNode.h"

class PathTracerNode final : public RenderPipelineNode {
public:
    PathTracerNode();

    std::string name() const override { return "Path tracer"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    bool m_shouldAccumulate { true };
    u32 m_currentAccumulatedFrames { 0 };
    u32 m_maxAccumulatedFrames { 1'000 };
};
