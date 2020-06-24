#include "rendering/CommandList.h"

#include "VulkanBackend.h"

class VulkanCommandList : public CommandList {
public:
    explicit VulkanCommandList(VulkanBackend&, VkCommandBuffer);

    void clearTexture(Texture&, ClearColor) override;

    void setRenderState(const RenderState&, ClearColor, float clearDepth, uint32_t clearStencil) override;
    void setRayTracingState(const RayTracingState&) override;
    void setComputeState(const ComputeState&) override;

    void bindSet(BindingSet&, uint32_t index) override;
    void pushConstants(ShaderStage, void*, size_t size, size_t byteOffset = 0u) override;

    void draw(Buffer& vertexBuffer, uint32_t vertexCount) override;
    void drawIndexed(Buffer& vertexBuffer, Buffer& indexBuffer, uint32_t indexCount, IndexType, uint32_t instanceIndex) override;
    
    void rebuildTopLevelAcceratationStructure(TopLevelAS&) override;
    void traceRays(Extent2D) override;

    void dispatch(Extent3D globalSize, Extent3D localSize) override;
    void dispatch(uint32_t x, uint32_t y, uint32_t z = 1) override;
    
    void waitEvent(uint8_t eventId, PipelineStage) override;
    void resetEvent(uint8_t eventId, PipelineStage) override;
    void signalEvent(uint8_t eventId, PipelineStage) override;

    void debugBarrier() override;

    void saveTextureToFile(const Texture&, const std::string&) override;

    void endNode(Badge<VulkanBackend>);

private:
    void endCurrentRenderPassIfAny();

    VulkanBackend& backend() { return m_backend; }

    VkDevice device() { return backend().device(); }
    VkPhysicalDevice physicalDevice() { return backend().physicalDevice(); }

    VkEvent getEvent(uint8_t eventId);
    VkPipelineStageFlags stageFlags(PipelineStage) const;

private:
    VulkanBackend& m_backend;
    VkCommandBuffer m_commandBuffer;

    const VulkanRenderState* activeRenderState = nullptr;
    const VulkanComputeState* activeComputeState = nullptr;
    const VulkanRayTracingState* activeRayTracingState = nullptr;
};
