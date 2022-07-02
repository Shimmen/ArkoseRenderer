#pragma once

#include "rendering/RenderPipelineNode.h"

class DepthOfFieldNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Depth of Field"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

    void setEnabled(bool enabled) { m_enabled = enabled; }

private:
    bool m_enabled { true };

    float m_maxBlurSize { 20.0f };
    float m_radiusScale { 0.85f };

    bool m_debugShowCircleOfConfusion { false };
    bool m_debugShowClampedBlurSize { false };
};
