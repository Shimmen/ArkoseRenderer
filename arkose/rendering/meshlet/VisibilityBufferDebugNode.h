#pragma once

#include "rendering/RenderPipelineNode.h"

class VisibilityBufferDebugNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Visibility buffer debug"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    enum class Mode {
        Drawables,
        Primitives,
    };

    Mode m_mode { Mode::Primitives };
};
