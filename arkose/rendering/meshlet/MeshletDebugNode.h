#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/meshlet/MeshletCuller.h"

class MeshletDebugNode final : public RenderPipelineNode {
public:
    std::string name() const override { return "Meshlet Debug"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    enum class RenderPath {
        VertexShader,
        MeshShaderDirect,
        MeshShaderIndirect,
    };

    RenderPath m_renderPath { RenderPath::MeshShaderIndirect };
    bool m_frustumCullInstances { false }; // Keep default off (for now!)
    bool m_frustumCullMeshlets { true };

    struct PassParams {
        static constexpr u32 groupSize { 32 }; // TODO: Get this value from the driver preferences!
        
        BindingSet* cameraBindingSet { nullptr };
        BindingSet* meshletTaskSetupBindingSet { nullptr };
        BindingSet* meshShaderBindingSet { nullptr };

        Buffer* indirectDataBuffer { nullptr };

        ComputeState* meshletTaskSetupState { nullptr };
        RenderState* renderState { nullptr };
    };

    // NOTE: This path is only for debugging, as it's crushingly slow for anything other than very small scenes
    PassParams const& createVertexShaderPath(GpuScene&, Registry&, RenderTarget&);
    void executeVertexShaderPath(PassParams const&, GpuScene&, CommandList&, UploadBuffer&) const;

    PassParams const& createMeshShaderPath(GpuScene&, Registry&, RenderTarget&, bool indirect);
    void executeMeshShaderDirectPath(PassParams const&, GpuScene&, CommandList&, UploadBuffer&) const;
    void executeMeshShaderIndirectPath(PassParams const&, GpuScene&, CommandList&, UploadBuffer&) const;
};
