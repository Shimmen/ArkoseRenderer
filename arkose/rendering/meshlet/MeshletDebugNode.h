#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/meshlet/MeshletIndirectHelper.h"

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
    MeshletIndirectHelper m_meshletIndirectHelper {};
    bool m_frustumCullMeshlets { true };

    struct PassParams {
        static constexpr u32 groupSize { 32 }; // TODO: Get this value from the driver preferences!
        MeshletIndirectSetupState const* meshletIndirectSetupState { nullptr };
        RenderState* renderState { nullptr };
    };

    // NOTE: This path is only for debugging, as it's crushingly slow for anything other than very small scenes
    PassParams const& createVertexShaderPath(GpuScene&, Registry&, RenderTarget&);
    void executeVertexShaderPath(PassParams const&, GpuScene&, CommandList&, UploadBuffer&) const;

    PassParams const& createMeshShaderPath(GpuScene&, Registry&, RenderTarget&, bool indirect);
    void executeMeshShaderDirectPath(PassParams const&, GpuScene&, CommandList&, UploadBuffer&) const;
    void executeMeshShaderIndirectPath(PassParams const&, GpuScene&, CommandList&, UploadBuffer&) const;
};
