#pragma once

#include "../RenderGraphNode.h"
#include "rendering/scene/Scene.h"

class SkyViewNode final : public RenderGraphNode {
public:
    explicit SkyViewNode(Scene&);

    static std::string name() { return "skyview"; }
    std::optional<std::string> displayName() const override { return "Sky view"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
};
