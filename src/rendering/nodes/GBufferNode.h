#pragma once

#include "../RenderGraphNode.h"
#include <rendering/scene/Scene.h>

class GBufferNode final : public RenderGraphNode {
public:
    explicit GBufferNode(const Scene&);

    static std::string name();
    std::optional<std::string> displayName() const override { return "G-buffer"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
};
