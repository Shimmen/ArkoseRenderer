#include "D3D12CommandList.h"

#include "utility/Profiling.h"
#include "rendering/backend/d3d12/D3D12Backend.h"
#include "rendering/backend/d3d12/D3D12Buffer.h"
#include <d3d12.h>

D3D12CommandList::D3D12CommandList(D3D12Backend& backend, ID3D12GraphicsCommandList* d3d12CommandList)
    : m_backend(backend)
    , m_commandList(d3d12CommandList)
{
}

void D3D12CommandList::fillBuffer(Buffer&, u32 fillValue)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::clearTexture(Texture& genColorTexture, ClearValue clearValue)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::copyTexture(Texture& genSrc, Texture& genDst, uint32_t srcMip, uint32_t dstMip)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::generateMipmaps(Texture& genTexture)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::executeBufferCopyOperations(std::vector<BufferCopyOperation> copyOperations)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::beginRendering(const RenderState& genRenderState, bool autoSetViewport)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::beginRendering(const RenderState& genRenderState, ClearValue clearValue, bool autoSetViewport)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::endRendering()
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::setRayTracingState(const RayTracingState& rtState)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::setComputeState(const ComputeState& genComputeState)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::evaluateUpscaling(UpscalingState const&, UpscalingParameters)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::bindTextureSet(BindingSet&, u32 index)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::setNamedUniform(const std::string& name, void* data, size_t size)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::draw(u32 vertexCount, u32 firstVertex)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    //if (!activeRenderState) {
    //    ARKOSE_LOG(Fatal, "draw: no active render state!");
    //}
    if (m_boundVertexBuffer == nullptr) {
        ARKOSE_LOG(Fatal, "draw: no bound vertex buffer!");
    }

    m_commandList->DrawInstanced(vertexCount, 1, firstVertex, 0);
}

void D3D12CommandList::drawIndexed(u32 indexCount, u32 instanceIndex)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    // if (!activeRenderState) {
    //     ARKOSE_LOG(Fatal, "draw: no active render state!");
    // }
    if (m_boundVertexBuffer == nullptr) {
        ARKOSE_LOG(Fatal, "draw: no bound vertex buffer!");
    }
    if (m_boundIndexBuffer == nullptr) {
        ARKOSE_LOG(Fatal, "draw: no bound index buffer!");
    }

    m_commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, instanceIndex);
}

void D3D12CommandList::drawIndirect(const Buffer& indirectBuffer, const Buffer& countBuffer)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::drawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::drawMeshTasksIndirect(Buffer const& indirectBuffer, u32 indirectDataStride, u32 indirectDataOffset,
                                             Buffer const& countBuffer, u32 countDataOffset)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::setViewport(ivec2 origin, ivec2 size)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::setDepthBias(float constantFactor, float slopeFactor)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::bindVertexBuffer(Buffer const& vertexBuffer, u32 stride, u32 bindingIdx)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (vertexBuffer.usage() != Buffer::Usage::Vertex) {
        ARKOSE_LOG(Fatal, "bindVertexBuffer: not a vertex buffer!");
    }

    ID3D12Resource* d3d12BufferResource = static_cast<D3D12Buffer const&>(vertexBuffer).bufferResource.Get();

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    vertexBufferView.BufferLocation = d3d12BufferResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBuffer.size());
    vertexBufferView.StrideInBytes = stride;

    m_commandList->IASetVertexBuffers(bindingIdx, 1, &vertexBufferView);

    m_boundVertexBuffer = d3d12BufferResource;
}

void D3D12CommandList::bindIndexBuffer(Buffer const& indexBuffer, IndexType indexType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (indexBuffer.usage() != Buffer::Usage::Index) {
        ARKOSE_LOG(Fatal, "bindIndexBuffer: not an index buffer!");
    }

    ID3D12Resource* d3d12BufferResource = static_cast<D3D12Buffer const&>(indexBuffer).bufferResource.Get();

    D3D12_INDEX_BUFFER_VIEW indexBufferView;
    indexBufferView.BufferLocation = d3d12BufferResource->GetGPUVirtualAddress();
    indexBufferView.SizeInBytes = static_cast<UINT>(indexBuffer.size());

    switch (indexType) {
    case IndexType::UInt16:
        indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        break;
    case IndexType::UInt32:
        indexBufferView.Format = DXGI_FORMAT_R32_UINT;
        break;
    default:
        ASSERT_NOT_REACHED();
        break;
    }

    m_commandList->IASetIndexBuffer(&indexBufferView);

    m_boundIndexBuffer = d3d12BufferResource;
}

void D3D12CommandList::issueDrawCall(DrawCallDescription const& drawCall)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::buildTopLevelAcceratationStructure(TopLevelAS& tlas, AccelerationStructureBuildType buildType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::buildBottomLevelAcceratationStructure(BottomLevelAS& blas, AccelerationStructureBuildType buildType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::traceRays(Extent2D extent)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::dispatch(Extent3D globalSize, Extent3D localSize)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::slowBlockingReadFromBuffer(const Buffer& buffer, size_t offset, size_t size, void* dst)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::debugBarrier()
{
}

void D3D12CommandList::beginDebugLabel(const std::string& scopeName)
{
}

void D3D12CommandList::endDebugLabel()
{
}

void D3D12CommandList::textureWriteBarrier(const Texture& genTexture)
{
}

void D3D12CommandList::textureMipWriteBarrier(const Texture& genTexture, uint32_t mip)
{
}

void D3D12CommandList::bufferWriteBarrier(std::vector<Buffer const*> buffers)
{
}
