#pragma once

#include "rendering/RenderPipelineNode.h"
#include <rendering/scene/Scene.h>

class GBufferNode final : public RenderPipelineNode {
public:
    explicit GBufferNode(const Scene&);

    static std::string name();
    std::optional<std::string> displayName() const override { return "G-buffer"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
};
