#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class TonemapNode final : public RenderPipelineNode {
public:

    enum class Mode {
        RenderToWindow,
        RenderToSceneColorLDR,
    };

    TonemapNode(Scene&, std::string sourceTextureName, Mode = Mode::RenderToSceneColorLDR);

    std::string name() const override { return "Tonemap"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
    std::string m_sourceTextureName;
    Mode m_mode;
};
