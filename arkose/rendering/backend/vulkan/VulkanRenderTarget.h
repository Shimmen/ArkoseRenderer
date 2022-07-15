#pragma once

#include "rendering/backend/base/RenderTarget.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

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
