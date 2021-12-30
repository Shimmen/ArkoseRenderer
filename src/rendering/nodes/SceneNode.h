#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class SceneNode final : public RenderGraphNode {
public:
    explicit SceneNode(Scene&);

    std::optional<std::string> displayName() const override { return "Scene"; }
    static std::string name();

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
};
