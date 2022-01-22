#pragma once

#include "rendering/RenderPipelineNode.h"
#include "RTData.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class RTDirectLightNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "RT direct light"; }
    ExecuteCallback construct(Scene&, Registry&) override;
};
