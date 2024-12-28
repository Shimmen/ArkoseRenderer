#include "VulkanRenderTarget.h"

#include "rendering/backend/vulkan/VulkanBackend.h"
#include "core/Logging.h"
#include "utility/Profiling.h"

VulkanRenderTarget::VulkanRenderTarget(Backend& backend, std::vector<Attachment> attachments)
    : RenderTarget(backend, std::move(attachments))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    VulkanBackend& vulkanBackend = static_cast<VulkanBackend&>(backend);
    VkDevice device = vulkanBackend.device();

    std::vector<VkImageView> allAttachmentImageViews {};
    std::vector<VkAttachmentDescription> allAttachments {};

    std::vector<VkAttachmentReference> colorAttachmentRefs {};
    std::optional<VkAttachmentReference> depthAttachmentRef {};
    std::vector<VkAttachmentReference> resolveAttachmentRefs {};

    auto createAttachmentDescription = [&](Texture* genTexture, VkImageLayout finalLayout, LoadOp loadOp, StoreOp storeOp) -> uint32_t {
        ARKOSE_ASSERT(genTexture);
        auto& texture = static_cast<VulkanTexture&>(*genTexture);

        VkAttachmentDescription attachment = {};
        attachment.format = texture.vkFormat;
        attachment.samples = static_cast<VkSampleCountFlagBits>(texture.multisampling());

        switch (loadOp) {
        case LoadOp::Load:
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.initialLayout = finalLayout;
            break;
        case LoadOp::Clear:
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
        case LoadOp::Discard:
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
        }

        switch (storeOp) {
        case StoreOp::Store:
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
            break;
        case StoreOp::Discard:
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            break;
        }

        attachment.finalLayout = finalLayout;

        uint32_t attachmentIndex = (uint32_t)allAttachments.size();
        allAttachments.push_back(attachment);
        allAttachmentImageViews.push_back(texture.imageView);

        return attachmentIndex;
    };

    auto createAttachmentData = [&](const Attachment attachInfo, bool considerResolve) -> VkAttachmentReference {
        VkImageLayout finalLayout = (attachInfo.type == AttachmentType::Depth)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        uint32_t attachmentIndex = createAttachmentDescription(attachInfo.texture, finalLayout, attachInfo.loadOp, attachInfo.storeOp);

        VkAttachmentReference attachmentRef = {};
        attachmentRef.attachment = attachmentIndex;
        attachmentRef.layout = finalLayout;

        if (considerResolve && attachInfo.multisampleResolveTexture) {

            // FIXME: Should we use "Don't care" for load op?
            uint32_t multisampleAttachmentIndex = createAttachmentDescription(attachInfo.multisampleResolveTexture, finalLayout, attachInfo.loadOp, attachInfo.storeOp);

            VkAttachmentReference multisampleAttachmentRef = {};
            attachmentRef.attachment = multisampleAttachmentIndex;
            attachmentRef.layout = finalLayout;

            resolveAttachmentRefs.push_back(multisampleAttachmentRef);
        }

        return attachmentRef;
    };

    for (const Attachment& colorAttachment : colorAttachments()) {

        ARKOSE_ASSERT((colorAttachment.texture->isMultisampled() && colorAttachment.multisampleResolveTexture)
                      || (!colorAttachment.texture->isMultisampled() && !colorAttachment.multisampleResolveTexture));

        VkAttachmentReference ref = createAttachmentData(colorAttachment, true);
        colorAttachmentRefs.push_back(ref);
    }

    if (hasDepthAttachment()) {
        VkAttachmentReference ref = createAttachmentData(depthAttachment().value(), false);
        depthAttachmentRef = ref;
    }

    // TODO: How do we want to support multiple subpasses in the future?
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = (uint32_t)colorAttachmentRefs.size();
    subpass.pColorAttachments = colorAttachmentRefs.data();
    subpass.pResolveAttachments = resolveAttachmentRefs.data();
    if (depthAttachmentRef.has_value()) {
        subpass.pDepthStencilAttachment = &depthAttachmentRef.value();
    }

    VkRenderPassCreateInfo renderPassCreateInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassCreateInfo.attachmentCount = (uint32_t)allAttachments.size();
    renderPassCreateInfo.pAttachments = allAttachments.data();
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpass;
    renderPassCreateInfo.dependencyCount = 0;
    renderPassCreateInfo.pDependencies = nullptr;

    if (vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &compatibleRenderPass) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create render pass");
    }

    VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebufferCreateInfo.renderPass = compatibleRenderPass;
    framebufferCreateInfo.attachmentCount = (uint32_t)allAttachmentImageViews.size();
    framebufferCreateInfo.pAttachments = allAttachmentImageViews.data();
    framebufferCreateInfo.width = extent().width();
    framebufferCreateInfo.height = extent().height();
    framebufferCreateInfo.layers = 1;

    bool makeImagelessFramebuffer = false;
    for (const Attachment& attachment : colorAttachments()) {
        if (attachment.texture == vulkanBackend.placeholderSwapchainTexture()) {
            makeImagelessFramebuffer = true;
            break;
        }
    }

    VkFramebufferAttachmentsCreateInfo attachmentCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO };
    std::vector<VkFramebufferAttachmentImageInfo> attachmentImageInfos {};
    if (makeImagelessFramebuffer) {
        framebufferIsImageless = true;

        forEachAttachmentInOrder([&attachmentImageInfos](const RenderTarget::Attachment& attachment) {
            VkFramebufferAttachmentImageInfo attachmentImageInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO };

            auto* texture = static_cast<VulkanTexture*>(attachment.texture);

            attachmentImageInfo.flags = 0;
            attachmentImageInfo.usage = texture->vkUsage;
            attachmentImageInfo.width = texture->extent().width();
            attachmentImageInfo.height = texture->extent().height();
            attachmentImageInfo.layerCount = 1;
            attachmentImageInfo.viewFormatCount = 1;
            attachmentImageInfo.pViewFormats = &texture->vkFormat;

            attachmentImageInfos.push_back(attachmentImageInfo);
        });

        attachmentCreateInfo.attachmentImageInfoCount = (uint32_t)attachmentImageInfos.size();
        attachmentCreateInfo.pAttachmentImageInfos = attachmentImageInfos.data();

        framebufferCreateInfo.flags |= VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
        framebufferCreateInfo.pNext = &attachmentCreateInfo;
    }

    if (vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create framebuffer");
    }

    for (auto& colorAttachment : colorAttachments()) {
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachedTextures.push_back({ colorAttachment.texture, finalLayout });
        if (colorAttachment.multisampleResolveTexture)
            attachedTextures.push_back({ colorAttachment.multisampleResolveTexture, finalLayout });
    }

    if (hasDepthAttachment()) {
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachedTextures.push_back({ depthAttachment().value().texture, finalLayout });
    }
}

VulkanRenderTarget::~VulkanRenderTarget()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vkDestroyFramebuffer(vulkanBackend.device(), framebuffer, nullptr);
    vkDestroyRenderPass(vulkanBackend.device(), compatibleRenderPass, nullptr);
}

void VulkanRenderTarget::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        std::string framebufferName = name + "-framebuffer";
        std::string renderPassName = name + "-renderPass";

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_FRAMEBUFFER;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(framebuffer);
            nameInfo.pObjectName = framebufferName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan framebuffer resource.");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_RENDER_PASS;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(compatibleRenderPass);
            nameInfo.pObjectName = renderPassName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan render pass resource.");
            }
        }
    }
}
