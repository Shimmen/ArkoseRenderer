#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class PickingNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "Picking"; }
    ExecuteCallback construct(Scene&, Registry&) override;

private:
    mutable std::optional<Buffer*> m_lastResultBuffer {};

    bool didClick(Button) const;
};
