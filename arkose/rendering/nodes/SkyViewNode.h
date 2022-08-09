#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

class SkyViewNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Sky view"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    bool m_skyViewEnabled { true };
};
