#pragma once

#include "rendering/RenderPipelineNode.h"

class RTReflectionsNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "RT reflections"; }
    ExecuteCallback construct(Scene&, Registry&) override;
};
