#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/meshlet/MeshletCuller.h"

class MeshletDebugNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Meshlet Debug"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    //MeshletCuller m_meshletCuller {};
};
