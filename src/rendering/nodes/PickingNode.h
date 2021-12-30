#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class PickingNode final : public RenderPipelineNode {
public:
    explicit PickingNode(Scene&);

    std::string name() const override { return "Picking"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
    mutable std::optional<Buffer*> m_lastResultBuffer {};

    bool didClick(Button) const;
};
