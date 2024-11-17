#pragma once

#include "rendering/RenderPipelineNode.h"

struct StaticMeshInstance;

class BakeAmbientOcclusionNode final : public RenderPipelineNode {
public:
    BakeAmbientOcclusionNode(StaticMeshInstance&, u32 meshLodIdxToBake, u32 meshSegmentIdxToBake, u32 sampleCount);
    ~BakeAmbientOcclusionNode() = default;

    std::string name() const override { return "Bake ambient occlusion"; }

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    StaticMeshInstance& m_instanceToBake;
    u32 m_meshLodIdxToBake { 0 };
    u32 m_meshSegmentIdxToBake { 0 };
    u32 m_sampleCount { 500 };
};
