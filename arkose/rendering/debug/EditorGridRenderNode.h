#pragma once

#include "rendering/RenderPipelineNode.h"

class EditorGridRenderNode final : public RenderPipelineNode {
public:
    EditorGridRenderNode() = default;
    ~EditorGridRenderNode() = default;

    std::string name() const override { return "Editor grid"; }

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    bool m_enabled { true };
};
