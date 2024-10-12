#pragma once

#include "rendering/RenderPipelineNode.h"

class EditorGridRenderNode final : public RenderPipelineNode {
public:
    EditorGridRenderNode() = default;
    ~EditorGridRenderNode() = default;

    std::string name() const override { return "Editor grid"; }

    ExecuteCallback construct(GpuScene&, Registry&) override;
};
