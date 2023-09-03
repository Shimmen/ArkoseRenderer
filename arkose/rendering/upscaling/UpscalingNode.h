#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/UpscalingParameters.h"

class UpscalingNode final : public RenderPipelineNode {
public:
    UpscalingNode(UpscalingTech, UpscalingQuality);

    std::string name() const override;
    void drawGui() override;

    virtual UpscalingTech upscalingTech() const override { return m_upscalingTech; }
    virtual UpscalingQuality upscalingQuality() const override { return m_upscalingQuality; }

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    UpscalingTech m_upscalingTech;
    UpscalingQuality m_upscalingQuality;
    UpscalingState* m_upscalingState { nullptr };
};
