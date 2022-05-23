#include "backend/base/Backend.h"

class D3D12Backend final : public Backend {
public:
    D3D12Backend(Badge<Backend>, GLFWwindow*, const AppSpecification& appSpecification);
    ~D3D12Backend() final;

    D3D12Backend(D3D12Backend&&) = delete;
    D3D12Backend(D3D12Backend&) = delete;
    D3D12Backend& operator=(D3D12Backend&) = delete;

    ///////////////////////////////////////////////////////////////////////////
    /// Public backend API

    bool hasActiveCapability(Capability) const override { return false; }

    ShaderDefine rayTracingShaderDefine() const override { return ShaderDefine(); }

    void renderPipelineDidChange(RenderPipeline&) override;
    void shadersDidRecompile(const std::vector<std::string>& shaderNames, RenderPipeline&) override;

    void newFrame() override;
    bool executeFrame(const Scene&, RenderPipeline&, float elapsedTime, float deltaTime) override;

    void shutdown() override;

    ///////////////////////////////////////////////////////////////////////////
    /// Backend-specific resource types

    std::unique_ptr<Buffer> createBuffer(size_t, Buffer::Usage, Buffer::MemoryHint) override;
    std::unique_ptr<RenderTarget> createRenderTarget(std::vector<RenderTarget::Attachment>) override;
    std::unique_ptr<Texture> createTexture(Texture::Description) override;
    std::unique_ptr<BindingSet> createBindingSet(std::vector<ShaderBinding>) override;
    std::unique_ptr<RenderState> createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, const StateBindings&,
                                                   const Viewport&, const BlendState&, const RasterState&, const DepthState&, const StencilState&) override;
    std::unique_ptr<ComputeState> createComputeState(const Shader&, std::vector<BindingSet*>) override;
    std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(std::vector<RTGeometry>) override;
    std::unique_ptr<TopLevelAS> createTopLevelAccelerationStructure(uint32_t maxInstanceCount, std::vector<RTGeometryInstance>) override;
    std::unique_ptr<RayTracingState> createRayTracingState(ShaderBindingTable& sbt, const StateBindings&, uint32_t maxRecursionDepth) override;

    ///////////////////////////////////////////////////////////////////////////
    /// ...

    // ...

private:
    ///////////////////////////////////////////////////////////////////////////
    /// Window and swapchain related members

    GLFWwindow* m_window;
    // swapchain etc.

    ///////////////////////////////////////////////////////////////////////////
    /// Frame management related members

    // ...

    ///////////////////////////////////////////////////////////////////////////
    /// Sub-systems / extensions

    // ...

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    // ...

};
