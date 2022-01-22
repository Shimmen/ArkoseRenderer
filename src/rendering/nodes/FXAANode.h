#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class FXAANode final : public RenderPipelineNode {
public:
    FXAANode(Scene&);

    std::string name() const override { return "FXAA"; }

    ExecuteCallback construct(Registry&) override;

private:
    Scene& m_scene;
};
