#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class GIComposeNode final : public RenderPipelineNode {
public:
    explicit GIComposeNode(Scene&);

    std::string name() const override { return "GI Compose"; }

    ExecuteCallback construct(Registry&) override;

private:
    Scene& m_scene;
};
