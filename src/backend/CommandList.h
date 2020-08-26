#pragma once

#include <backend/Resources.h>
#include <string>

class CommandList {
public:
    virtual void clearTexture(Texture&, ClearColor) = 0;
    virtual void copyTexture(Texture& src, Texture& dst) = 0;
    virtual void generateMipmaps(Texture&) = 0;

    virtual void beginRendering(const RenderState&, ClearColor, float clearDepth, uint32_t clearStencil = 0) = 0;
    virtual void endRendering() = 0;

    virtual void setRayTracingState(const RayTracingState&) = 0;
    virtual void setComputeState(const ComputeState&) = 0;

    virtual void bindSet(BindingSet&, uint32_t index) = 0;
    virtual void pushConstants(ShaderStage, void*, size_t size, size_t byteOffset = 0u) = 0;

    template<typename T>
    void pushConstant(ShaderStage, T, size_t byteOffset = 0u);

    virtual void draw(Buffer& vertexBuffer, uint32_t vertexCount) = 0;
    virtual void drawIndexed(const Buffer& vertexBuffer, const Buffer& indexBuffer, uint32_t indexCount, IndexType, uint32_t instanceIndex = 0) = 0;

    virtual void rebuildTopLevelAcceratationStructure(TopLevelAS&) = 0;
    virtual void traceRays(Extent2D) = 0;

    virtual void dispatch(Extent3D globalSize, Extent3D localSize) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z = 1) = 0;

    virtual void waitEvent(uint8_t eventId, PipelineStage) = 0;
    virtual void resetEvent(uint8_t eventId, PipelineStage) = 0;
    virtual void signalEvent(uint8_t eventId, PipelineStage) = 0;

    //! A barrier for all commands and memory, which probably only should be used for debug stuff.
    virtual void debugBarrier() = 0;

    virtual void textureWriteBarrier(const Texture&) = 0;

    virtual void slowBlockingReadFromBuffer(const Buffer&, size_t offset, size_t size, void* dst) = 0;

    virtual void saveTextureToFile(const Texture&, const std::string&) = 0;
};

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
