#pragma once

#include "../RenderGraphNode.h"
#include "rendering/scene/Scene.h"

class BloomNode final : public RenderGraphNode {
public:
    explicit BloomNode(Scene&);

    static std::string name() { return "bloom"; }
    std::optional<std::string> displayName() const override { return "Bloom"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
};
