#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class RTFirstHitNode final : public RenderPipelineNode {
public:
    explicit RTFirstHitNode(Scene&);
    ~RTFirstHitNode() override = default;

    std::string name() const override { return "RT first-hit"; }

    ExecuteCallback construct(Registry&) override;

private:
    Scene& m_scene;
};
