#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class TAANode final : public RenderPipelineNode {
public:
    TAANode(Scene&);

    std::string name() const override { return "TAA"; }

    ExecuteCallback construct(Registry&) override;

private:
    Scene& m_scene;

    bool m_taaEnabled { true };
    bool m_taaEnabledPreviousFrame { false };
};
