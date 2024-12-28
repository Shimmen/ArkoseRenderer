#pragma once

#include "rendering/backend/base/Texture.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanTexture final : public Texture {
public:
    VulkanTexture() = default;
    VulkanTexture(Backend&, Description);
    virtual ~VulkanTexture() override;

    // Specifically used to create a texture from a swapchain image, which we do not own and also do not yet know the exact image & imageView for.
    static std::unique_ptr<VulkanTexture> createSwapchainPlaceholderTexture(Extent2D swapchainExtent, VkImageUsageFlags imageUsage, VkFormat swapchainFormat);

    virtual void setName(const std::string& name) override;

    virtual bool storageCapable() const override;

    void clear(ClearColor) override;

    void setData(const void* data, size_t size, size_t mipIdx, size_t arrayIdx) override;
    std::unique_ptr<ImageAsset> copyDataToImageAsset(u32 mipIdx) override;

    void generateMipmaps() override;

    uint32_t layerCount() const;

    VkImageAspectFlags aspectMask() const;

    VkImageView createImageView(uint32_t baseMip, uint32_t numMips, std::optional<VkComponentMapping>) const;

    VkImage image { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };

    VkImageUsageFlags vkUsage { 0 };
    VkFormat vkFormat { VK_FORMAT_R8G8B8A8_UINT };
    VkImageView imageView { VK_NULL_HANDLE };
    VkSampler sampler { VK_NULL_HANDLE };

    mutable VkImageLayout currentLayout { VK_IMAGE_LAYOUT_UNDEFINED };

    // For Dear ImGui display
    static constexpr VkImageLayout ImGuiRenderingTargetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    static std::vector<VulkanTexture*> texturesForImGuiRendering;
    VkImageView imageViewNoAlphaForImGui { VK_NULL_HANDLE };
    VkDescriptorSet descriptorSetForImGui { VK_NULL_HANDLE };
    virtual ImTextureID asImTextureID() override;
};
