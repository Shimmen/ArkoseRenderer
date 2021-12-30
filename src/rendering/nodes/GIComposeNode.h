#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class GIComposeNode final : public RenderGraphNode {
public:
    explicit GIComposeNode(Scene&);

    std::optional<std::string> displayName() const override { return "GI Compose"; }
    static std::string name();

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
};
