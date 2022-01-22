#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/Scene.h"

class BloomNode final : public RenderPipelineNode {
public:
    explicit BloomNode(Scene&);

    std::string name() const override { return "Bloom"; }

    ExecuteCallback construct(Registry&) override;

private:
    Scene& m_scene;
};
