#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class FinalNode final : public RenderPipelineNode {
public:
    FinalNode(Scene&, std::string sourceTextureName);

    std::string name() const override { return "Final"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
    std::string m_sourceTextureName;
};
