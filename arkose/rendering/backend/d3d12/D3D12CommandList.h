#include "rendering/backend/base/CommandList.h"

class D3D12Backend;
struct D3D12Buffer;
struct D3D12ComputeState;
struct D3D12RenderState;
struct D3D12Texture;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

#if defined(TRACY_ENABLE)
#include <d3d12.h>
#include <tracy/TracyD3D12.hpp>
#endif

class D3D12CommandList final : public CommandList {
public:
    D3D12CommandList(D3D12Backend&, ID3D12GraphicsCommandList*);

    void fillBuffer(Buffer&, u32 fillValue) override;
    void clearTexture(Texture&, ClearValue) override;
    void copyTexture(Texture& src, Texture& dst, u32 srcMip, u32 dstMip) override;
    void generateMipmaps(Texture&) override;

    void executeBufferCopyOperations(std::vector<BufferCopyOperation>) override;

    void beginRendering(const RenderState&, bool autoSetViewport) override;
    void beginRendering(const RenderState&, ClearValue, bool autoSetViewport) override;
    void endRendering() override;

    void setRayTracingState(const RayTracingState&) override;
    void setComputeState(const ComputeState&) override;

    void evaluateUpscaling(UpscalingState const&, UpscalingParameters) override;

    void bindTextureSet(BindingSet&, u32 index) override;

    void setNamedUniform(const std::string& name, void const* data, size_t size) override;

    void draw(u32 vertexCount, u32 firstVertex) override;
    void drawIndexed(u32 indexCount, u32 instanceIndex) override;
    void drawIndirect(const Buffer& indirectBuffer, const Buffer& countBuffer) override;

    void drawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override;
    void drawMeshTasksIndirect(Buffer const& indirectBuffer, u32 indirectDataStride, u32 indirectDataOffset,
                               Buffer const& countBuffer, u32 countDataOffset) override;

    void setViewport(ivec2 origin, ivec2 size) override;
    void setDepthBias(float constantFactor, float slopeFactor) override;

    void bindVertexBuffer(Buffer const&, size_t stride, u32 bindingIdx) override;
    void bindIndexBuffer(Buffer const&, IndexType) override;
    void issueDrawCall(const DrawCallDescription&) override;

    void buildTopLevelAcceratationStructure(TopLevelAS&, AccelerationStructureBuildType) override;
    void buildBottomLevelAcceratationStructure(BottomLevelAS&, AccelerationStructureBuildType) override;
    void traceRays(Extent2D) override;

    void dispatch(uint32_t x, uint32_t y, uint32_t z = 1) override;

    void debugBarrier() override;
    void beginDebugLabel(const std::string&) override;
    void endDebugLabel() override;

    void textureWriteBarrier(Texture const&) override;
    void textureMipWriteBarrier(Texture const&, u32 mip) override;
    void bufferWriteBarrier(std::vector<Buffer const*>) override;

    void slowBlockingReadFromBuffer(const Buffer&, size_t offset, size_t size, void* dst) override;

private:
    D3D12Backend& backend() { return m_backend; }

    D3D12_RESOURCE_BARRIER createResourceTransitionBarrier(D3D12Buffer const&, D3D12_RESOURCE_STATES targetResourceState) const;
    D3D12_RESOURCE_BARRIER createResourceTransitionBarrier(D3D12Texture const&, D3D12_RESOURCE_STATES targetResourceState) const;
    void createTransitionBarriersForAllReferencedResources(StateBindings const&, std::vector<D3D12_RESOURCE_BARRIER>& outBarriers) const;

    D3D12Backend& m_backend;
    ID3D12GraphicsCommandList* m_commandList;

    ID3D12Resource* m_boundVertexBuffer { nullptr };
    ID3D12Resource* m_boundIndexBuffer { nullptr };

    D3D12RenderState const* m_activeRenderState { nullptr };
    D3D12ComputeState const* m_activeComputeState { nullptr };

#if defined(TRACY_ENABLE)
    std::vector<std::unique_ptr<tracy::D3D12ZoneScope>> m_tracyDebugLabelStack;
#endif

};
