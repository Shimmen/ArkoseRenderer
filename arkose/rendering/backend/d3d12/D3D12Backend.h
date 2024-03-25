#include "rendering/backend/base/Backend.h"

#include "rendering/backend/d3d12/D3D12Common.h"
#include "rendering/backend/d3d12/D3D12Texture.h"
struct IDXGISwapChain4;
struct D3D12RenderTarget;
class D3D12DescriptorHeapAllocator;


#if defined(TRACY_ENABLE)
#include <tracy/TracyD3D12.hpp>
#endif

static constexpr bool d3d12debugMode = true;

class D3D12Backend final : public Backend {
public:
    D3D12Backend(Badge<Backend>, const AppSpecification& appSpecification);
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
    bool executeFrame(RenderPipeline&, float elapsedTime, float deltaTime) override;

    void completePendingOperations() override;

    virtual bool hasUpscalingSupport() const override { return false; }

    ///////////////////////////////////////////////////////////////////////////
    /// Backend-specific resource types

    std::unique_ptr<Buffer> createBuffer(size_t, Buffer::Usage, Buffer::MemoryHint) override;
    std::unique_ptr<RenderTarget> createRenderTarget(std::vector<RenderTarget::Attachment>) override;
    std::unique_ptr<Texture> createTexture(Texture::Description) override;
    std::unique_ptr<BindingSet> createBindingSet(std::vector<ShaderBinding>) override;
    std::unique_ptr<RenderState> createRenderState(RenderTarget const&, std::vector<VertexLayout> const&, Shader const&, StateBindings const&,
                                                   RasterState const&, DepthState const&, StencilState const&) override;
    std::unique_ptr<ComputeState> createComputeState(Shader const&, StateBindings const&) override;
    std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(std::vector<RTGeometry>, BottomLevelAS const* copySource = nullptr) override;
    std::unique_ptr<TopLevelAS> createTopLevelAccelerationStructure(uint32_t maxInstanceCount, std::vector<RTGeometryInstance>) override;
    std::unique_ptr<RayTracingState> createRayTracingState(ShaderBindingTable& sbt, const StateBindings&, uint32_t maxRecursionDepth) override;
    std::unique_ptr<UpscalingState> createUpscalingState(UpscalingTech, UpscalingQuality, Extent2D renderRes, Extent2D outputDisplayRes) override;

    ///////////////////////////////////////////////////////////////////////////
    /// Utilities

    ID3D12Device& device() { return *m_device.Get(); }
    ID3D12Device& device() const { return *m_device.Get(); }

    IDXGISwapChain4& swapChain() { return *m_swapChain.Get(); }
    ID3D12CommandQueue& commandQueue() { return *m_commandQueue.Get(); }

    void waitForFence(ID3D12Fence* fence, UINT64 completionValue, HANDLE waitEvent) const;
    void waitForDeviceIdle();

    bool setBufferDataUsingMapping(ID3D12Resource&, const uint8_t* data, size_t size, size_t offset = 0);
    bool setBufferDataUsingStagingBuffer(struct D3D12Buffer&, const uint8_t* data, size_t size, size_t offset = 0);

    void issueUploadCommand(const std::function<void(ID3D12GraphicsCommandList&)>& callback) const;

    D3D12DescriptorHeapAllocator& copyableDescriptorHeapAllocator();
    D3D12DescriptorHeapAllocator& shaderVisibleDescriptorHeapAllocator();

    #if defined(TRACY_ENABLE)
    tracy::D3D12QueueCtx* tracyD3D12Context() { return m_tracyD3D12Context; }
    #endif

private:
    ///////////////////////////////////////////////////////////////////////////
    /// Utility functions

    ComPtr<ID3D12Device> createDeviceAtMaxSupportedFeatureLevel() const;
    ComPtr<ID3D12CommandQueue> createDefaultCommandQueue() const;
    ComPtr<IDXGISwapChain4> createSwapChain(ID3D12CommandQueue*) const;

    void createWindowRenderTarget();
    void recreateSwapChain();

    ///////////////////////////////////////////////////////////////////////////
    /// Window and swapchain related members

    Extent2D m_windowFramebufferExtent { 0, 0 };

    ComPtr<ID3D12Device> m_device {};
    ComPtr<ID3D12DebugDevice> m_debugDevice {};

    ComPtr<ID3D12CommandQueue> m_commandQueue {};

    ComPtr<IDXGISwapChain4> m_swapChain {};
    static constexpr DXGI_FORMAT SwapChainFormat                 = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT SwapChainRenderTargetViewFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_SWAP_CHAIN_FLAG SwapChainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    ///////////////////////////////////////////////////////////////////////////
    /// Frame management related members

    static constexpr int QueueSlotCount = 2;

    u32 m_currentFrameIndex { 0 };
    u32 m_relativeFrameIndex { 0 };
    u32 m_nextSwapchainBufferIndex { 0 };

    UINT64 m_nextSequentialFenceValue { 1 };

    struct FrameContext {
        ComPtr<ID3D12Fence> frameFence;
        HANDLE frameFenceEvent;
        UINT64 frameFenceValue;

        ComPtr<ID3D12CommandAllocator> commandAllocator;
        ComPtr<ID3D12GraphicsCommandList> commandList;

        ComPtr<ID3D12Resource> renderTarget;

        std::unique_ptr<UploadBuffer> uploadBuffer {};
    };

    std::array<std::unique_ptr<FrameContext>, QueueSlotCount> m_frameContexts {};

    ComPtr<ID3D12DescriptorHeap> m_renderTargetDescriptorHeap;
    int32_t m_renderTargetViewDescriptorSize {};

    std::unique_ptr<D3D12Texture> m_swapchainDepthTexture {};
    std::unique_ptr<D3D12Texture> m_mockSwapchainTexture {};
    std::unique_ptr<D3D12RenderTarget> m_mockWindowRenderTarget {};

    ///////////////////////////////////////////////////////////////////////////
    /// Resource & resource management members

    // NOTE: CBV/SRV/UAV is implied here to save some typing. Assume if the code says just "descriptor" it's a CBV/SRV/UAV.
    std::unique_ptr<D3D12DescriptorHeapAllocator> m_copyableDescriptorHeapAllocator { nullptr };
    std::unique_ptr<D3D12DescriptorHeapAllocator> m_shaderVisibleDescriptorHeapAllocator { nullptr };

    std::unique_ptr<Registry> m_pipelineRegistry {};

    #if defined(TRACY_ENABLE)
    tracy::D3D12QueueCtx* m_tracyD3D12Context {};
    #endif

};
