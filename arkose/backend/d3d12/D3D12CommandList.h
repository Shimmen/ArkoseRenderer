#include "backend/base/CommandList.h"

#include "backend/d3d12/D3D12Backend.h"

class D3D12CommandList final : public CommandList {
public:
    explicit D3D12CommandList(D3D12Backend&);

    void clearTexture(Texture&, ClearColor) override;
    void copyTexture(Texture& src, Texture& dst, uint32_t srcLayer = 0, uint32_t dstLayer = 0) override;
    void generateMipmaps(Texture&) override;

    void executeBufferCopyOperations(std::vector<BufferCopyOperation>) override;

    void beginRendering(const RenderState&) override;
    void beginRendering(const RenderState&, ClearColor, float clearDepth, uint32_t clearStencil) override;
    void endRendering() override;

    void setRayTracingState(const RayTracingState&) override;
    void setComputeState(const ComputeState&) override;

    void bindSet(BindingSet&, uint32_t index) override;
    void pushConstants(ShaderStage, void*, size_t size, size_t byteOffset = 0u) override;
    void setNamedUniform(const std::string& name, void* data, size_t size) override;

    void draw(Buffer& vertexBuffer, uint32_t vertexCount) override;
    void drawIndexed(const Buffer& vertexBuffer, const Buffer& indexBuffer, uint32_t indexCount, IndexType, uint32_t instanceIndex) override;
    void drawIndirect(const Buffer& indirectBuffer, const Buffer& countBuffer) override;

    void bindVertexBuffer(const Buffer&) override;
    void bindIndexBuffer(const Buffer&, IndexType) override;
    void issueDrawCall(const DrawCallDescription&) override;

    void buildTopLevelAcceratationStructure(TopLevelAS&, AccelerationStructureBuildType) override;
    void traceRays(Extent2D) override;

    void dispatch(Extent3D globalSize, Extent3D localSize) override;
    void dispatch(uint32_t x, uint32_t y, uint32_t z = 1) override;

    void debugBarrier() override;
    void beginDebugLabel(const std::string&) override;
    void endDebugLabel() override;

    void textureWriteBarrier(const Texture&) override;
    void bufferWriteBarrier(std::vector<Buffer*>) override;

    void slowBlockingReadFromBuffer(const Buffer&, size_t offset, size_t size, void* dst) override;

    void saveTextureToFile(const Texture&, const std::string&) override;

private:
    D3D12Backend& backend() { return m_backend; }

private:
    D3D12Backend& m_backend;

};