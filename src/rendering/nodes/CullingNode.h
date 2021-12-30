#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class CullingNode final : public RenderPipelineNode {
public:
    explicit CullingNode(Scene&);

    std::string name() const override { return "Culling"; }

    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;
};
