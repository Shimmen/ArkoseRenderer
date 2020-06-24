#pragma once

#include "../RenderGraphNode.h"
#include "rendering/scene/Scene.h"

class FinalPostFxNode final : public RenderGraphNode {
public:
    explicit FinalPostFxNode(const Scene&);
    ~FinalPostFxNode() override = default;

    std::optional<std::string> displayName() const override { return "Final PostFX"; }

    static std::string name();
    ExecuteCallback constructFrame(Registry&) const override;

private:
    const Scene& m_scene;
};
