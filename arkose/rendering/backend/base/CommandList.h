#pragma once

#include "rendering/backend/Resources.h"
#include "rendering/backend/util/DrawCall.h"
#include "rendering/backend/util/UploadBuffer.h"
#include <string>

class CommandList {
public:
    virtual void fillBuffer(Buffer&, u32 fillValue) = 0;
    virtual void clearTexture(Texture&, ClearValue) = 0;
    virtual void copyTexture(Texture& src, Texture& dst, uint32_t srcMip = 0, uint32_t dstMip = 0) = 0;
    virtual void generateMipmaps(Texture&) = 0;

    virtual void executeBufferCopyOperations(UploadBuffer&);
    virtual void executeBufferCopyOperations(std::vector<BufferCopyOperation>) = 0;

    virtual void beginRendering(const RenderState&, bool autoSetViewport = true) = 0;
    virtual void beginRendering(const RenderState&, ClearValue, bool autoSetViewport = true) = 0;
    virtual void endRendering() = 0;

    virtual void setRayTracingState(const RayTracingState&) = 0;
    virtual void setComputeState(const ComputeState&) = 0;

    virtual void evaluateUpscaling(UpscalingState const&, UpscalingParameters) = 0;

    // In general we don't want to be rebinding a bunch of textures while rendering, as we support bindless
    // throughout, but there are some cases where being able to just bind a texture directly is very useful.
    // This function allows you to bind a binding set consisting of only sampled textures, with a layout
    // matching your shader. Note that it's your own responsibility to ensure that the textures are in a
    // suitable state for being sampled, as this function will NOT transition any textures.
    virtual void bindTextureSet(BindingSet&, u32 index) = 0;

    virtual void setNamedUniform(const std::string& name, void*, size_t size) = 0;

    template<typename T>
    void setNamedUniform(const std::string& name, T);

    virtual void draw(u32 vertexCount, u32 firstVertex = 0) = 0;
    virtual void drawIndexed(u32 indexCount, u32 instanceIndex = 0) = 0;
    virtual void drawIndirect(const Buffer& indirectBuffer, const Buffer& countBuffer) = 0;

    virtual void drawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ) = 0;
    virtual void drawMeshTasksIndirect(Buffer const& indirectBuffer, u32 indirectDataStride, u32 indirectDataOffset,
                                       Buffer const& countBuffer, u32 countDataOffset) = 0;

    virtual void setViewport(ivec2 origin, ivec2 size) = 0;
    virtual void setDepthBias(float constantFactor, float slopeFactor) = 0;

    virtual void bindVertexBuffer(Buffer const&, size_t stride, u32 bindingIdx) = 0;
    virtual void bindIndexBuffer(Buffer const&, IndexType) = 0;
    virtual void issueDrawCall(const DrawCallDescription&) = 0;

    virtual void buildTopLevelAcceratationStructure(TopLevelAS&, AccelerationStructureBuildType) = 0;
    virtual void buildBottomLevelAcceratationStructure(BottomLevelAS&, AccelerationStructureBuildType) = 0;
    virtual void traceRays(Extent2D) = 0;

    void dispatch(Extent3D globalSize, Extent3D localSize);
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z = 1) = 0;

    //! A barrier for all commands and memory, which probably only should be used for debug stuff.
    virtual void debugBarrier() = 0;

    //! Debug scopes for display in e.g. RenderDoc
    virtual void beginDebugLabel(const std::string&) = 0;
    virtual void endDebugLabel() = 0;

    virtual void textureWriteBarrier(const Texture&) = 0;
    virtual void textureMipWriteBarrier(const Texture&, uint32_t mip) = 0;
    virtual void bufferWriteBarrier(std::vector<Buffer const*>) = 0;

    virtual void slowBlockingReadFromBuffer(const Buffer&, size_t offset, size_t size, void* dst) = 0;
};

inline void CommandList::executeBufferCopyOperations(UploadBuffer& uploadBuffer)
{
    executeBufferCopyOperations(uploadBuffer.popPendingOperations());
}

template<typename T>
inline void CommandList::setNamedUniform(const std::string& name, T value)
{
    setNamedUniform(name, &value, sizeof(T));
}

template<>
inline void CommandList::setNamedUniform(const std::string& name, bool value)
{
    uint32_t intValue = (value) ? 1 : 0;
    setNamedUniform(name, &intValue, sizeof(uint32_t));
}

inline void CommandList::dispatch(Extent3D globalSize, Extent3D localSize)
{
    u32 x = (globalSize.width() + localSize.width() - 1) / localSize.width();
    u32 y = (globalSize.height() + localSize.height() - 1) / localSize.height();
    u32 z = (globalSize.depth() + localSize.depth() - 1) / localSize.depth();
    dispatch(x, y, z);
}
