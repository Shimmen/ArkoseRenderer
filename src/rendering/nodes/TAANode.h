#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class TAANode final : public RenderPipelineNode {
public:
    TAANode(Scene&);

    std::string name() const override { return "TAA"; }

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;

    mutable bool m_taaEnabled { true };
    mutable bool m_taaEnabledPreviousFrame { false };

    Texture* m_accumulationTexture { nullptr };
};
