#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

class Camera;

class TAANode final : public RenderPipelineNode {
public:
    TAANode(Camera&);

    std::string name() const override { return "TAA"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

    void setEnabled(bool enabled) { m_taaEnabled = enabled; }

private:
    bool m_taaEnabled { true };
    bool m_taaEnabledPreviousFrame { false };

    float m_hysteresis { 0.95f };
    bool m_useCatmullRom { true };
};
