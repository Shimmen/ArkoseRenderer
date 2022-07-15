#pragma once

#include "rendering/backend/Resources.h"
#include "rendering/backend/util/DrawCall.h"
#include "rendering/backend/util/UploadBuffer.h"
#include <string>

class CommandList {
public:
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

    virtual void bindSet(BindingSet&, uint32_t index) = 0;
    virtual void pushConstants(ShaderStage, void*, size_t size, size_t byteOffset = 0u) = 0;
    virtual void setNamedUniform(const std::string& name, void*, size_t size) = 0;

    template<typename T>
    void pushConstant(ShaderStage, T, size_t byteOffset = 0u);

    template<typename T>
    void setNamedUniform(const std::string& name, T);

    virtual void draw(Buffer& vertexBuffer, uint32_t vertexCount) = 0;
    virtual void drawIndexed(const Buffer& vertexBuffer, const Buffer& indexBuffer, uint32_t indexCount, IndexType, uint32_t instanceIndex = 0) = 0;
    virtual void drawIndirect(const Buffer& indirectBuffer, const Buffer& countBuffer) = 0;

    virtual void setViewport(ivec2 origin, ivec2 size) = 0;

    virtual void bindVertexBuffer(const Buffer&) = 0;
    virtual void bindIndexBuffer(const Buffer&, IndexType) = 0;
    virtual void issueDrawCall(const DrawCallDescription&) = 0;

    virtual void buildTopLevelAcceratationStructure(TopLevelAS&, AccelerationStructureBuildType) = 0;
    virtual void traceRays(Extent2D) = 0;

    virtual void dispatch(Extent3D globalSize, Extent3D localSize) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z = 1) = 0;

    //! A barrier for all commands and memory, which probably only should be used for debug stuff.
    virtual void debugBarrier() = 0;

    //! Debug scopes for display in e.g. RenderDoc
    virtual void beginDebugLabel(const std::string&) = 0;
    virtual void endDebugLabel() = 0;

    virtual void textureWriteBarrier(const Texture&) = 0;
    virtual void textureMipWriteBarrier(const Texture&, uint32_t mip) = 0;
    virtual void bufferWriteBarrier(std::vector<Buffer*>) = 0;

    virtual void slowBlockingReadFromBuffer(const Buffer&, size_t offset, size_t size, void* dst) = 0;

    virtual void saveTextureToFile(const Texture&, const std::string&) = 0;
};

inline void CommandList::executeBufferCopyOperations(UploadBuffer& uploadBuffer)
{
    executeBufferCopyOperations(uploadBuffer.popPendingOperations());
}

template<typename T>
inline void CommandList::pushConstant(ShaderStage shaderStage, T value, size_t byteOffset)
{
    pushConstants(shaderStage, &value, sizeof(T), byteOffset);
}

template<>
inline void CommandList::pushConstant(ShaderStage shaderStage, bool value, size_t byteOffset)
{
    uint32_t intValue = (value) ? 1 : 0;
    pushConstant(shaderStage, intValue, byteOffset);
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
