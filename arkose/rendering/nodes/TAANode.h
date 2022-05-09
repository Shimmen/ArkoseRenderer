#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/GpuScene.h"

class Camera;

class TAANode final : public RenderPipelineNode {
public:
    TAANode(Camera&);

    std::string name() const override { return "TAA"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    bool m_taaEnabled { true };
    bool m_taaEnabledPreviousFrame { false };
};
