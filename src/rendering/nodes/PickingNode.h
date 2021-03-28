#pragma once

#include "../RenderGraphNode.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class PickingNode final : public RenderGraphNode {
public:
    explicit PickingNode(Scene&);

    std::optional<std::string> displayName() const override { return "Picking"; }

    static std::string name();
    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
    mutable std::optional<Buffer*> m_lastResultBuffer {};

    bool didClick(Button) const;
};
