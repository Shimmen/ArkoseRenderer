#include "rendering/backend/base/CommandList.h"

#include "VulkanBackend.h"

#if defined(TRACY_ENABLE)
#include <tracy/TracyVulkan.hpp>
#endif

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

    void evaluateUpscaling(UpscalingState const&, UpscalingParameters) override;

    void bindTextureSet(BindingSet&, u32 index) override;

    void setNamedUniform(const std::string& name, void* data, size_t size) override;

    void draw(u32 vertexCount, u32 firstVertex) override;
    void drawIndexed(u32 indexCount, u32 instanceIndex) override;
    void drawIndirect(const Buffer& indirectBuffer, const Buffer& countBuffer) override;

    void drawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override;
    void drawMeshTasksIndirect(Buffer const& indirectBuffer, u32 indirectDataStride, u32 indirectDataOffset,
                               Buffer const& countBuffer, u32 countDataOffset) override;

    void setViewport(ivec2 origin, ivec2 size) override;
    void setDepthBias(float constantFactor, float slopeFactor) override;

    void bindVertexBuffer(const Buffer&, u32 stride, u32 bindingIdx) override;
    void bindIndexBuffer(const Buffer&, IndexType) override;
    void issueDrawCall(const DrawCallDescription&) override;

    void buildTopLevelAcceratationStructure(TopLevelAS&, AccelerationStructureBuildType) override;
    void buildBottomLevelAcceratationStructure(BottomLevelAS&, AccelerationStructureBuildType) override;
    void traceRays(Extent2D) override;

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
    void bindSet(BindingSet&, u32 index);

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

    const VulkanRenderState* activeRenderState = nullptr;
    const VulkanComputeState* activeComputeState = nullptr;
    const RayTracingState* activeRayTracingState = nullptr;

#if defined(TRACY_ENABLE)
    std::vector<std::unique_ptr<tracy::VkCtxScope>> m_tracyDebugLabelStack;
#endif
};
