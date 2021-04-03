#pragma once

#include "../RenderGraphNode.h"
#include "rendering/scene/Scene.h"

class CullingNode final : public RenderGraphNode {
public:
    explicit CullingNode(Scene&);

    std::optional<std::string> displayName() const override { return "Culling"; }
    static std::string name();

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
};
