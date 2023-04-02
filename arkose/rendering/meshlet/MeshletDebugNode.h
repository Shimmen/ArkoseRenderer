#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/meshlet/MeshletCuller.h"

class MeshletDebugNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Meshlet Debug"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    //MeshletCuller m_meshletCuller {};

    struct MeshShaderPathParams {
        static constexpr u32 groupSize { 32 }; // TODO: Get this value from the driver preferences!
        
        BindingSet* cameraBindingSet { nullptr };
        BindingSet* meshletTaskSetupBindingSet { nullptr };
        BindingSet* meshShaderBindingSet { nullptr };

        Buffer* taskShaderCmdsBuffer { nullptr };
        Buffer* taskShaderCountBuffer { nullptr };
        Buffer* drawableLookupBuffer { nullptr };

        ComputeState* meshletTaskSetupState { nullptr };
        RenderState* meshShaderRenderState { nullptr };
    };

    MeshShaderPathParams const& createMeshShaderPath(GpuScene&, Registry&, RenderTarget&);
    void executeMeshShaderPath(MeshShaderPathParams const&, GpuScene&, CommandList&, UploadBuffer&) const;
};
