#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class FinalNode final : public RenderPipelineNode {
public:
    explicit FinalNode(std::string sourceTextureName);

    std::string name() const override { return "Final"; }
    ExecuteCallback construct(Scene&, Registry&) override;

private:
    std::string m_sourceTextureName;
};
