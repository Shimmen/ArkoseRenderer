#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/scene/GpuScene.h"

class BloomNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Bloom"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

    static constexpr size_t _NumDownsamples = 6;
    static constexpr size_t NumMipLevels = _NumDownsamples + 1;
    static constexpr size_t BottomMipLevel = NumMipLevels - 1;

private:
    std::vector<BindingSet*> m_downsampleSets {};
    std::vector<BindingSet*> m_upsampleSets {};
};
