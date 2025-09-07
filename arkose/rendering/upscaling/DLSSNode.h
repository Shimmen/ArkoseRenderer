#pragma once

#if WITH_DLSS

#include "rendering/RenderPipelineNode.h"
#include "rendering/UpscalingParameters.h"

class DLSSNode final : public RenderPipelineNode {
public:
    DLSSNode(UpscalingQuality);

    static bool isSupported();

    std::string name() const override { return "DLSS"; }
    void drawGui() override;

    virtual UpscalingTech upscalingTech() const override { return UpscalingTech::DLSS; }
    virtual UpscalingQuality upscalingQuality() const override { return m_upscalingQuality; }

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    UpscalingQuality m_upscalingQuality;
    ExternalFeature* m_dlssFeature { nullptr };

    bool m_enabled { true };
    bool m_controlGlobalMipBias { true };
};

#endif // WITH_DLSS
