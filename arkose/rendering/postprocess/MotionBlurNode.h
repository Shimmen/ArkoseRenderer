#pragma once

#include "core/Types.h"
#include "rendering/RenderPipelineNode.h"
#include <string>

class MotionBlurNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Motion Blur"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

    static constexpr u32 TileSize = 16;

private:
    bool m_enabled { true };

    float m_shutterAngleDegrees { 180.0f };

    u32 m_maxBlurRadiusPixels { TileSize };
    u32 m_sampleCount { 15 };
    float m_softZExtent { 0.02f };
};
