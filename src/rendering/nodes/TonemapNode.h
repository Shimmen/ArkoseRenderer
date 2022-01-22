#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class TonemapNode final : public RenderPipelineNode {
public:

    enum class Mode {
        RenderToWindow,
        RenderToSceneColorLDR,
    };

    TonemapNode(std::string sourceTextureName, Mode = Mode::RenderToSceneColorLDR);

    std::string name() const override { return "Tonemap"; }
    ExecuteCallback construct(Scene&, Registry&) override;

private:
    std::string m_sourceTextureName;
    Mode m_mode;
};
