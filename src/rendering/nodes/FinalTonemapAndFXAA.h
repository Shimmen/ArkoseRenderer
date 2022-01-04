#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class FinalTonemapAndFXAA final : public RenderPipelineNode {
public:
    FinalTonemapAndFXAA(Scene&, std::string sourceTextureName);

    std::string name() const override { return "Final (tonemap & FXAA)"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
    std::string m_sourceTextureName;
};
