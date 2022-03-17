#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/GpuScene.h"

class PickingNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "Picking"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    mutable std::optional<Buffer*> m_lastResultBuffer {};

    bool didClick(Button) const;
};
