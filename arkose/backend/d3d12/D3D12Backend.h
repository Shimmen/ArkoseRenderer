#include "backend/base/Backend.h"

#include "backend/d3d12/D3D12Common.h"
#include <dxgi.h>

static constexpr bool d3d12debugMode = true;

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
    /// Utilities

    ID3D12Device& device() { return *m_device.Get(); }
    ID3D12Device& device() const { return *m_device.Get(); }

    IDXGISwapChain& swapChain() { return *m_swapChain.Get(); }
    ID3D12CommandQueue& commandQueue() { return *m_commandQueue.Get(); }

    void waitForFence(ID3D12Fence* fence, UINT64 completionValue, HANDLE waitEvent) const;

    bool setBufferDataUsingMapping(ID3D12Resource&, const uint8_t* data, size_t size, size_t offset = 0);
    bool setBufferDataUsingStagingBuffer(struct D3D12Buffer&, const uint8_t* data, size_t size, size_t offset = 0);

    void issueUploadCommand(const std::function<void(ID3D12GraphicsCommandList&)>& callback) const;

private:
    ///////////////////////////////////////////////////////////////////////////
    /// Window and swapchain related members

    GLFWwindow* m_window;
    
    ComPtr<ID3D12Device> m_device {};

    ComPtr<IDXGISwapChain> m_swapChain {};
    Extent2D m_swapChainExtent {};

    int32_t m_renderTargetViewDescriptorSize {};

    ComPtr<ID3D12CommandQueue> m_commandQueue {};

    static constexpr DXGI_FORMAT SwapChainFormat                 = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT SwapChainRenderTargetViewFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    ///////////////////////////////////////////////////////////////////////////
    /// Frame management related members

    static constexpr int QueueSlotCount = 3;

    uint32_t m_currentFrameIndex { 0 };
    uint32_t m_relativeFrameIndex { 0 };

    UINT64 m_nextSequentialFenceValue { 1 };

    struct FrameContext {
        ComPtr<ID3D12Fence> frameFence;
        HANDLE frameFenceEvent;
        UINT64 frameFenceValue;

        ComPtr<ID3D12CommandAllocator> commandAllocator;
        ComPtr<ID3D12GraphicsCommandList> commandList;

        ComPtr<ID3D12Resource> renderTarget;
    };

    std::array<std::unique_ptr<FrameContext>, QueueSlotCount> m_frameContexts {};

    ComPtr<ID3D12DescriptorHeap> m_renderTargetDescriptorHeap;

    ///////////////////////////////////////////////////////////////////////////
    /// Demo stuff

    struct Demo {

        ComPtr<ID3D12RootSignature> rootSignature;
        ComPtr<ID3D12PipelineState> pso;
    
        std::unique_ptr<D3D12Buffer> vertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

        std::unique_ptr<D3D12Buffer> indexBuffer;
        D3D12_INDEX_BUFFER_VIEW indexBufferView;

        const char shaders[1542] = "#ifdef D3D12_SAMPLE_BASIC\n"
                                   "struct VertexShaderOutput\n"
                                   "{\n"
                                   "	float4 position : SV_POSITION;\n"
                                   "	float2 uv : TEXCOORD;\n"
                                   "};\n"
                                   "\n"
                                   "VertexShaderOutput VS_main(\n"
                                   "	float4 position : POSITION,\n"
                                   "	float2 uv : TEXCOORD)\n"
                                   "{\n"
                                   "	VertexShaderOutput output;\n"
                                   "\n"
                                   "	output.position = position;\n"
                                   "	output.uv = uv;\n"
                                   "\n"
                                   "	return output;\n"
                                   "}\n"
                                   "\n"
                                   "float4 PS_main (float4 position : SV_POSITION,\n"
                                   "				float2 uv : TEXCOORD) : SV_TARGET\n"
                                   "{\n"
                                   "	return float4(uv, 0, 1);\n"
                                   "}\n"
                                   "#elif D3D12_SAMPLE_CONSTANT_BUFFER\n"
                                   "cbuffer PerFrameConstants : register (b0)\n"
                                   "{\n"
                                   "	float4 scale;\n"
                                   "}\n"
                                   "\n"
                                   "struct VertexShaderOutput\n"
                                   "{\n"
                                   "	float4 position : SV_POSITION;\n"
                                   "	float2 uv : TEXCOORD;\n"
                                   "};\n"
                                   "\n"
                                   "VertexShaderOutput VS_main(\n"
                                   "	float4 position : POSITION,\n"
                                   "	float2 uv : TEXCOORD)\n"
                                   "{\n"
                                   "	VertexShaderOutput output;\n"
                                   "\n"
                                   "	output.position = position;\n"
                                   "	output.position.xy *= scale.x;\n"
                                   "	output.uv = uv;\n"
                                   "\n"
                                   "	return output;\n"
                                   "}\n"
                                   "\n"
                                   "float4 PS_main (float4 position : SV_POSITION,\n"
                                   "				float2 uv : TEXCOORD) : SV_TARGET\n"
                                   "{\n"
                                   "	return float4(uv, 0, 1);\n"
                                   "}\n"
                                   "#elif D3D12_SAMPLE_TEXTURE\n"
                                   "cbuffer PerFrameConstants : register (b0)\n"
                                   "{\n"
                                   "	float4 scale;\n"
                                   "}\n"
                                   "\n"
                                   "struct VertexShaderOutput\n"
                                   "{\n"
                                   "	float4 position : SV_POSITION;\n"
                                   "	float2 uv : TEXCOORD;\n"
                                   "};\n"
                                   "\n"
                                   "VertexShaderOutput VS_main(\n"
                                   "	float4 position : POSITION,\n"
                                   "	float2 uv : TEXCOORD)\n"
                                   "{\n"
                                   "	VertexShaderOutput output;\n"
                                   "\n"
                                   "	output.position = position;\n"
                                   "	output.position.xy *= scale.x;\n"
                                   "	output.uv = uv;\n"
                                   "\n"
                                   "	return output;\n"
                                   "}\n"
                                   "\n"
                                   "Texture2D<float4> anteruTexture : register(t0);\n"
                                   "SamplerState texureSampler      : register(s0);\n"
                                   "\n"
                                   "float4 PS_main (float4 position : SV_POSITION,\n"
                                   "				float2 uv : TEXCOORD) : SV_TARGET\n"
                                   "{\n"
                                   "	return anteruTexture.Sample (texureSampler, uv);\n"
                                   "}\n"
                                   "#endif\n";

    } m_demo;

};