#pragma once

#include "../RenderGraphNode.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class PickingNode final : public RenderGraphNode {
public:
    explicit PickingNode(Scene&);

    std::optional<std::string> displayName() const override { return "Picking"; }

    static std::string name();
    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
    std::vector<std::pair<Buffer*, Buffer*>> m_meshBuffers {};
    mutable std::optional<vec2> m_mouseDownLocation {};
};
