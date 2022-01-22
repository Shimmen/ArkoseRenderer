#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class SceneNode final : public RenderPipelineNode {
public:
    explicit SceneNode(Scene&);

    std::string name() const override { return "Scene"; }

    ExecuteCallback construct(Registry&) override;

private:
    Scene& m_scene;
};
