#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class RTDirectLightNode final : public RenderPipelineNode {
public:
    explicit RTDirectLightNode(Scene&);
    ~RTDirectLightNode() override = default;

    std::string name() const override { return "RT direct light"; }

    ExecuteCallback construct(Registry&) override;

private:
    Scene& m_scene;
};
