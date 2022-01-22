#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class CullingNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Culling"; }
    ExecuteCallback construct(Scene&, Registry&) override;
};
