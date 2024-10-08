#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/GpuScene.h"

class BloomNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Bloom"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

    static constexpr size_t _NumDownsamples = 6;
    static constexpr size_t NumMipLevels = _NumDownsamples + 1;
    static constexpr size_t BottomMipLevel = NumMipLevels - 1;

private:
    std::vector<ComputeState*> m_downsampleStates {};
    std::vector<ComputeState*> m_upsampleStates {};

    bool m_enabled { true };
    float m_upsampleBlurRadius { 0.0036f };
    float m_bloomBlend { 0.04f };
};
