#include "D3D12CommandList.h"

#include "utility/Profiling.h"

D3D12CommandList::D3D12CommandList(D3D12Backend& backend)
    : m_backend(backend)
{
}

void D3D12CommandList::clearTexture(Texture& genColorTexture, ClearColor color)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::copyTexture(Texture& genSrc, Texture& genDst, uint32_t srcLayer, uint32_t dstLayer)
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

void D3D12CommandList::beginRendering(const RenderState& genRenderState)
{
}

void D3D12CommandList::beginRendering(const RenderState& genRenderState, ClearColor clearColor, float clearDepth, uint32_t clearStencil)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::endRendering()
{
}

void D3D12CommandList::setRayTracingState(const RayTracingState& rtState)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::setComputeState(const ComputeState& genComputeState)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::bindSet(BindingSet& bindingSet, uint32_t index)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::pushConstants(ShaderStage shaderStage, void* data, size_t size, size_t byteOffset)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::setNamedUniform(const std::string& name, void* data, size_t size)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::draw(Buffer& vertexBuffer, uint32_t vertexCount)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::drawIndexed(const Buffer& vertexBuffer, const Buffer& indexBuffer, uint32_t indexCount, IndexType indexType, uint32_t instanceIndex)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::drawIndirect(const Buffer& indirectBuffer, const Buffer& countBuffer)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::bindVertexBuffer(const Buffer& vertexBuffer)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::bindIndexBuffer(const Buffer& indexBuffer, IndexType indexType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::issueDrawCall(const DrawCallDescription& drawCall)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();
}

void D3D12CommandList::buildTopLevelAcceratationStructure(TopLevelAS& tlas, AccelerationStructureBuildType buildType)
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

void D3D12CommandList::saveTextureToFile(const Texture& texture, const std::string& filePath)
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

void D3D12CommandList::bufferWriteBarrier(std::vector<Buffer*> buffers)
{
}