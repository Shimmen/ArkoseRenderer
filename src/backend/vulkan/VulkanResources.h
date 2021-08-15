#pragma once

#include <backend/Resources.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanBuffer final : public Buffer {
public:
    VulkanBuffer(Backend&, size_t size, Usage, MemoryHint);
    virtual ~VulkanBuffer() override;

    virtual void setName(const std::string& name) override;

    void updateData(const std::byte* data, size_t size, size_t offset) override;
    void reallocateWithSize(size_t newSize, ReallocateStrategy) override;

    VkBuffer buffer;
    VmaAllocation allocation;

private:
    void createInternal(size_t size, VkBuffer& buffer, VmaAllocation& allocation);
    void destroyInternal(VkBuffer buffer, VmaAllocation allocation);
};

struct VulkanTexture final : public Texture {
public:
    VulkanTexture() = default;
    VulkanTexture(Backend&, TextureDescription);
    virtual ~VulkanTexture() override;

    virtual void setName(const std::string& name) override;

    void setPixelData(vec4 pixel) override;
    void setData(const void* data, size_t size) override;

    void generateMipmaps() override;

    uint32_t layerCount() const;

    VkImageAspectFlags aspectMask() const;

    VkImage image { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };

    VkImageUsageFlags vkUsage { 0 };
    VkFormat vkFormat { VK_FORMAT_R8G8B8A8_UINT };
    VkImageView imageView { VK_NULL_HANDLE };
    VkSampler sampler { VK_NULL_HANDLE };

    VkImageLayout currentLayout { VK_IMAGE_LAYOUT_UNDEFINED };
};

struct VulkanRenderTarget final : public RenderTarget {
public:
    enum class QuirkMode {
        None,
        ForPresenting,
    };

    VulkanRenderTarget() = default;
    explicit VulkanRenderTarget(Backend&, std::vector<Attachment> attachments, bool imageless, QuirkMode = QuirkMode::None);
    virtual ~VulkanRenderTarget() override;

    virtual void setName(const std::string& name) override;

    VkFramebuffer framebuffer { VK_NULL_HANDLE };
    VkRenderPass compatibleRenderPass { VK_NULL_HANDLE };

    std::vector<std::pair<Texture*, VkImageLayout>> attachedTextures;

    bool framebufferIsImageless { false };
    std::vector<VkImageView> imagelessFramebufferAttachments {};

    QuirkMode quirkMode;
};

struct VulkanBindingSet : public BindingSet {
public:
    VulkanBindingSet(Backend&, std::vector<ShaderBinding>);
    virtual ~VulkanBindingSet() override;

    virtual void setName(const std::string& name) override;

    VkDescriptorSetLayout createDescriptorSetLayout() const;

    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;
};

struct VulkanRenderState final : public RenderState {
public:
    VulkanRenderState(Backend&, const RenderTarget&, VertexLayout, Shader, const std::vector<BindingSet*>&,
                      Viewport, BlendState, RasterState, DepthState, StencilState);
    virtual ~VulkanRenderState() override;

    virtual void setName(const std::string& name) override;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    std::vector<Texture*> sampledTextures;
};

struct VulkanTopLevelAS final : public TopLevelAS {
public:
    VulkanTopLevelAS(Backend&, std::vector<RTGeometryInstance>);
    virtual ~VulkanTopLevelAS() override;

    virtual void setName(const std::string& name) override;

    VkAccelerationStructureNV accelerationStructure;
    VkDeviceMemory memory { VK_NULL_HANDLE };
    uint64_t handle { 0u };

    std::vector<std::pair<VkBuffer, VmaAllocation>> associatedBuffers;
};

struct VulkanBottomLevelAS final : public BottomLevelAS {
public:
    VulkanBottomLevelAS(Backend&, std::vector<RTGeometry>);
    virtual ~VulkanBottomLevelAS() override;

    virtual void setName(const std::string& name) override;

    VkAccelerationStructureNV accelerationStructure;
    VkDeviceMemory memory { VK_NULL_HANDLE };
    uint64_t handle { 0u };

    std::vector<std::pair<VkBuffer, VmaAllocation>> associatedBuffers;
};

struct VulkanRayTracingState final : public RayTracingState {
public:
    VulkanRayTracingState(Backend&, ShaderBindingTable, std::vector<BindingSet*>, uint32_t maxRecursionDepth);
    virtual ~VulkanRayTracingState() override;

    virtual void setName(const std::string& name) override;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    VkBuffer sbtBuffer;
    VmaAllocation sbtBufferAllocation;

    std::vector<Texture*> sampledTextures;
    std::vector<Texture*> storageImages;
};

struct VulkanComputeState final : public ComputeState {
public:
    VulkanComputeState(Backend&, Shader, std::vector<BindingSet*>);
    virtual ~VulkanComputeState() override;

    virtual void setName(const std::string& name) override;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    std::vector<Texture*> sampledTextures;
    std::vector<Texture*> storageImages;
};
