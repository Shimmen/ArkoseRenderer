#include "rendering/backend/base/CommandList.h"

#include "VulkanBackend.h"

class VulkanCommandList final : public CommandList {
public:
    explicit VulkanCommandList(VulkanBackend&, VkCommandBuffer);

    void fillBuffer(Buffer&, u32 fillValue) override;
    void clearTexture(Texture&, ClearValue) override;
    void copyTexture(Texture& src, Texture& dst, uint32_t srcMip = 0, uint32_t dstMip = 0) override;
    void generateMipmaps(Texture&) override;

    void executeBufferCopyOperations(std::vector<BufferCopyOperation>) override;

    void beginRendering(const RenderState&, bool autoSetViewport) override;
    void beginRendering(const RenderState&, ClearValue, bool autoSetViewport) override;
    void endRendering() override;

    void setRayTracingState(const RayTracingState&) override;
    void setComputeState(const ComputeState&) override;

    void bindSet(BindingSet&, uint32_t index) override;
    void pushConstants(ShaderStage, void*, size_t size, size_t byteOffset = 0u) override;
    void setNamedUniform(const std::string& name, void* data, size_t size) override;

    void draw(Buffer& vertexBuffer, uint32_t vertexCount, uint32_t firstVertex) override;
    void drawIndexed(const Buffer& vertexBuffer, const Buffer& indexBuffer, uint32_t indexCount, IndexType, uint32_t instanceIndex) override;
    void drawIndirect(const Buffer& indirectBuffer, const Buffer& countBuffer) override;

    void drawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override;
    void drawMeshTasksIndirect(Buffer const& indirectBuffer, u32 indirectDataStride, u32 indirectDataOffset,
                               Buffer const& countBuffer, u32 countDataOffset) override;

    void setViewport(ivec2 origin, ivec2 size) override;
    void setDepthBias(float constantFactor, float slopeFactor) override;

    void bindVertexBuffer(const Buffer&, u32 bindingIdx) override;
    void bindIndexBuffer(const Buffer&, IndexType) override;
    void issueDrawCall(const DrawCallDescription&) override;

    void buildTopLevelAcceratationStructure(TopLevelAS&, AccelerationStructureBuildType) override;
    void buildBottomLevelAcceratationStructure(BottomLevelAS&, AccelerationStructureBuildType) override;
    void traceRays(Extent2D) override;

    void dispatch(Extent3D globalSize, Extent3D localSize) override;
    void dispatch(uint32_t x, uint32_t y, uint32_t z = 1) override;

    void debugBarrier() override;
    void beginDebugLabel(const std::string&) override;
    void endDebugLabel() override;

    void textureWriteBarrier(const Texture&) override;
    void textureMipWriteBarrier(const Texture&, uint32_t mip) override;
    void bufferWriteBarrier(std::vector<Buffer const*>) override;

    void slowBlockingReadFromBuffer(const Buffer&, size_t offset, size_t size, void* dst) override;

    void endNode(Badge<VulkanBackend>);

private:
    void endCurrentRenderPassIfAny();

    VulkanBackend& backend() { return m_backend; }

    VkDevice device() { return backend().device(); }
    VkPhysicalDevice physicalDevice() { return backend().physicalDevice(); }

    // TODO: Remove this.. Make something more fine grained
    void transitionImageLayoutDEBUG(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags, VkCommandBuffer) const;

    void requireExactlyOneStateToBeSet(const std::string& context) const;
    std::pair<VkPipelineLayout, VkPipelineBindPoint> getCurrentlyBoundPipelineLayout();
    const Shader& getCurrentlyBoundShader();

private:
    VulkanBackend& m_backend;
    VkCommandBuffer m_commandBuffer;

    VkBuffer m_boundVertexBuffer { VK_NULL_HANDLE };
    VkBuffer m_boundIndexBuffer { VK_NULL_HANDLE };
    IndexType m_boundIndexBufferType {};

    const VulkanRenderState* activeRenderState = nullptr;
    const VulkanComputeState* activeComputeState = nullptr;
    const RayTracingState* activeRayTracingState = nullptr;
};
