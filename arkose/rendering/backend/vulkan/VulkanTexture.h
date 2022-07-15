#pragma once

#include "rendering/backend/base/Texture.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanTexture final : public Texture {
public:
    VulkanTexture() = default;
    VulkanTexture(Backend&, Description);
    virtual ~VulkanTexture() override;

    virtual void setName(const std::string& name) override;

    void clear(ClearColor) override;

    void setPixelData(vec4 pixel) override;
    void setData(const void* data, size_t size) override;

    void generateMipmaps() override;

    uint32_t layerCount() const;

    VkImageAspectFlags aspectMask() const;

    VkImageView createImageView(uint32_t baseMip, uint32_t numMips) const;

    VkImage image { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };

    VkImageUsageFlags vkUsage { 0 };
    VkFormat vkFormat { VK_FORMAT_R8G8B8A8_UINT };
    VkImageView imageView { VK_NULL_HANDLE };
    VkSampler sampler { VK_NULL_HANDLE };

    VkImageLayout currentLayout { VK_IMAGE_LAYOUT_UNDEFINED };
};
