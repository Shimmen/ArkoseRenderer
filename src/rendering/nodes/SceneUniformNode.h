#pragma once

#include "../RenderGraphNode.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class SceneUniformNode final : public RenderGraphNode {
public:
    explicit SceneUniformNode(const Scene&);

    std::optional<std::string> displayName() const override { return "Scene Uniforms"; }

    static std::string name();
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;
};
