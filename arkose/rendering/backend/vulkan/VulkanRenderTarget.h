#pragma once

#include "rendering/backend/base/RenderTarget.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanRenderTarget final : public RenderTarget {
public:
    VulkanRenderTarget() = default;
    VulkanRenderTarget(Backend&, std::vector<Attachment> attachments);
    virtual ~VulkanRenderTarget() override;

    virtual void setName(const std::string& name) override;

    VkFramebuffer framebuffer { VK_NULL_HANDLE };
    VkRenderPass compatibleRenderPass { VK_NULL_HANDLE };

    std::vector<std::pair<Texture*, VkImageLayout>> attachedTextures;

    bool framebufferIsImageless { false };
};
