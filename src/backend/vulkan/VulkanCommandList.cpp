#include "VulkanCommandList.h"

#include "VulkanBackend.h"
#include "VulkanResources.h"
#include "utility/Logging.h"
#include <stb_image_write.h>

VulkanCommandList::VulkanCommandList(VulkanBackend& backend, VkCommandBuffer commandBuffer)
    : m_backend(backend)
    , m_commandBuffer(commandBuffer)
{
}

void VulkanCommandList::clearTexture(Texture& genColorTexture, ClearColor color)
{
    auto& colorTexture = static_cast<VulkanTexture&>(genColorTexture);
    ASSERT(!colorTexture.hasDepthFormat());

    std::optional<VkImageLayout> originalLayout;
    if (colorTexture.currentLayout != VK_IMAGE_LAYOUT_GENERAL && colorTexture.currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        originalLayout = colorTexture.currentLayout;

        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = originalLayout.value();
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = colorTexture.image;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = colorTexture.mipLevels();
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = colorTexture.layerCount();

        // FIXME: Probably overly aggressive barriers!

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        vkCmdPipelineBarrier(m_commandBuffer,
                             sourceStage, destinationStage, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &imageBarrier);
    }

    VkClearColorValue clearValue {};
    clearValue.float32[0] = color.r;
    clearValue.float32[1] = color.g;
    clearValue.float32[2] = color.b;
    clearValue.float32[3] = color.a;

    VkImageSubresourceRange range {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    range.baseMipLevel = 0;
    range.levelCount = colorTexture.mipLevels();

    range.baseArrayLayer = 0;
    range.layerCount = colorTexture.layerCount();

    vkCmdClearColorImage(m_commandBuffer, colorTexture.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &range);

    if (originalLayout.has_value() && originalLayout.value() != VK_IMAGE_LAYOUT_UNDEFINED && originalLayout.value() != VK_IMAGE_LAYOUT_PREINITIALIZED) {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.newLayout = originalLayout.value();
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = colorTexture.image;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = colorTexture.mipLevels();
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = colorTexture.layerCount();

        // FIXME: Probably overly aggressive barriers!

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        vkCmdPipelineBarrier(m_commandBuffer,
                             sourceStage, destinationStage, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &imageBarrier);
    }
}

void VulkanCommandList::copyTexture(Texture& genSrc, Texture& genDst, uint32_t srcLayer, uint32_t dstLayer)
{
    auto& src = static_cast<VulkanTexture&>(genSrc);
    auto& dst = static_cast<VulkanTexture&>(genDst);

    ASSERT(!src.hasMipmaps() && !dst.hasMipmaps());

    ASSERT(src.hasDepthFormat() == dst.hasDepthFormat());
    VkImageAspectFlags aspectMask = src.hasDepthFormat()
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;

    ASSERT(src.currentLayout != VK_IMAGE_LAYOUT_UNDEFINED && src.currentLayout != VK_IMAGE_LAYOUT_PREINITIALIZED);
    VkImageLayout initialSrcLayout = src.currentLayout;

    VkImageLayout finalDstLayout = dst.currentLayout;
    if (finalDstLayout == VK_IMAGE_LAYOUT_UNDEFINED || finalDstLayout == VK_IMAGE_LAYOUT_PREINITIALIZED)
        finalDstLayout = VK_IMAGE_LAYOUT_GENERAL;

    {
        VkImageMemoryBarrier genBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        genBarrier.subresourceRange.aspectMask = aspectMask;
        genBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        genBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        genBarrier.subresourceRange.baseMipLevel = 0;
        genBarrier.subresourceRange.levelCount = 1;
        genBarrier.subresourceRange.baseArrayLayer = 0;
        genBarrier.subresourceRange.layerCount = 1;
        genBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

        std::array<VkImageMemoryBarrier, 2> barriers { genBarrier, genBarrier };

        barriers[0].image = src.image;
        barriers[0].oldLayout = src.currentLayout;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barriers[0].subresourceRange.baseArrayLayer = srcLayer;

        barriers[1].image = dst.image;
        barriers[1].oldLayout = dst.currentLayout;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[1].subresourceRange.baseArrayLayer = dstLayer;

        vkCmdPipelineBarrier(m_commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             barriers.size(), barriers.data());
    }

    {
        auto extentToOffset = [](Extent3D extent) -> VkOffset3D {
            return {
                static_cast<int32_t>(extent.width()),
                static_cast<int32_t>(extent.height()),
                static_cast<int32_t>(extent.depth()),
            };
        };

        VkImageBlit blit = {};

        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = extentToOffset(src.extent3D());
        blit.srcSubresource.aspectMask = aspectMask;
        blit.srcSubresource.mipLevel = 0;
        blit.srcSubresource.baseArrayLayer = srcLayer;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = extentToOffset(dst.extent3D());
        blit.dstSubresource.aspectMask = aspectMask;
        blit.dstSubresource.mipLevel = 0;
        blit.dstSubresource.baseArrayLayer = dstLayer;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(m_commandBuffer,
                       src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit,
                       VK_FILTER_LINEAR);
    }

    {
        VkImageMemoryBarrier genBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        genBarrier.subresourceRange.aspectMask = aspectMask;
        genBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        genBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        genBarrier.subresourceRange.baseMipLevel = 0;
        genBarrier.subresourceRange.levelCount = 1;
        genBarrier.subresourceRange.baseArrayLayer = 0;
        genBarrier.subresourceRange.layerCount = 1;
        genBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        genBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        std::array<VkImageMemoryBarrier, 2> barriers { genBarrier, genBarrier };

        barriers[0].image = src.image;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].newLayout = initialSrcLayout;
        barriers[0].subresourceRange.baseArrayLayer = srcLayer;

        barriers[1].image = dst.image;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].newLayout = finalDstLayout;
        barriers[1].subresourceRange.baseArrayLayer = dstLayer;

        vkCmdPipelineBarrier(m_commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             barriers.size(), barriers.data());
    }
}

void VulkanCommandList::generateMipmaps(Texture& genTexture)
{
    auto& texture = static_cast<VulkanTexture&>(genTexture);

    if (!texture.hasMipmaps()) {
        LogError("generateMipmaps called on command list for texture which doesn't have space for mipmaps allocated. Ignoring request.\n");
        return;
    }

    if (texture.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        LogError("generateMipmaps called on command list for texture which currently has the layout VK_IMAGE_LAYOUT_UNDEFINED. Ignoring request.\n");
        return;
    }

    // Make sure that all mips have whatever layout the texture has before this function was called!
    VkImageLayout finalLayout = texture.currentLayout;

    VkImageAspectFlagBits aspectMask = texture.hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.image = texture.image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = texture.layerCount();
    barrier.subresourceRange.levelCount = 1;

    uint32_t levels = texture.mipLevels();
    int32_t mipWidth = texture.extent().width();
    int32_t mipHeight = texture.extent().height();

    // We have to be very general in this function..
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkAccessFlags finalAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    // Transition mip 0 to transfer src optimal (and wait for all its read & writes to finish first)
    {
        VkImageMemoryBarrier initialBarrierMip0 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        initialBarrierMip0.image = texture.image;
        initialBarrierMip0.subresourceRange.aspectMask = aspectMask;
        initialBarrierMip0.oldLayout = texture.currentLayout;
        initialBarrierMip0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        initialBarrierMip0.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        initialBarrierMip0.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        initialBarrierMip0.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        initialBarrierMip0.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        initialBarrierMip0.subresourceRange.baseArrayLayer = 0;
        initialBarrierMip0.subresourceRange.layerCount = texture.layerCount();
        initialBarrierMip0.subresourceRange.baseMipLevel = 0;
        initialBarrierMip0.subresourceRange.levelCount = 1;

        vkCmdPipelineBarrier(m_commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &initialBarrierMip0);
    }

    // Transition mips 1-n to transfer dst optimal
    {
        VkImageMemoryBarrier initialBarrierMip1plus = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        initialBarrierMip1plus.image = texture.image;
        initialBarrierMip1plus.subresourceRange.aspectMask = aspectMask;
        initialBarrierMip1plus.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        initialBarrierMip1plus.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        initialBarrierMip1plus.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        initialBarrierMip1plus.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        initialBarrierMip1plus.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        initialBarrierMip1plus.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        initialBarrierMip1plus.subresourceRange.baseArrayLayer = 0;
        initialBarrierMip1plus.subresourceRange.layerCount = texture.layerCount();
        initialBarrierMip1plus.subresourceRange.baseMipLevel = 1;
        initialBarrierMip1plus.subresourceRange.levelCount = levels - 1;

        vkCmdPipelineBarrier(m_commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &initialBarrierMip1plus);
    }

    for (uint32_t i = 1; i < levels; ++i) {

        int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        // (mip0 is already in src optimal)
        if (i > 1) {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(m_commandBuffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);
        }

        VkImageBlit blit = {};

        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = aspectMask;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = texture.layerCount();

        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
        blit.dstSubresource.aspectMask = aspectMask;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = texture.layerCount();

        vkCmdBlitImage(m_commandBuffer,
                       texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit,
                       VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = finalLayout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = finalAccess;

        vkCmdPipelineBarrier(m_commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    barrier.subresourceRange.baseMipLevel = levels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = finalLayout;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = finalAccess;

    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
}

void VulkanCommandList::beginRendering(const RenderState& genRenderState)
{
    if (activeRenderState) {
        LogWarning("setRenderState: already active render state!\n");
        endCurrentRenderPassIfAny();
    }

    genRenderState.renderTarget().forEachAttachmentInOrder([](const RenderTarget::Attachment& attachment) {
        if (attachment.loadOp == LoadOp::Clear) {
            LogErrorAndExit("CommandList: calling beginRendering (with no extra arguments) for rendering to a render target with LoadOp::Clear textures. "
                            "For these render targets always use beginRendering with clear colors etc. specified. Exiting!");
        }
    });

    // NOTE: These will not be used, but we need to pass something in for the current API
    ClearColor clearColor { 0, 0, 0, 1 };
    float clearDepth = 1.0f;
    uint32_t clearStencil = 0;

    beginRendering(genRenderState, clearColor, clearDepth, clearStencil);
}

void VulkanCommandList::beginRendering(const RenderState& genRenderState, ClearColor clearColor, float clearDepth, uint32_t clearStencil)
{
    if (activeRenderState) {
        LogWarning("setRenderState: already active render state!\n");
        endCurrentRenderPassIfAny();
    }
    auto& renderState = static_cast<const VulkanRenderState&>(genRenderState);
    activeRenderState = &renderState;

    activeRayTracingState = nullptr;
    activeComputeState = nullptr;

    auto& renderTarget = static_cast<const VulkanRenderTarget&>(renderState.renderTarget());

    std::vector<VkClearValue> clearValues {};
    renderTarget.forEachAttachmentInOrder([&](const RenderTarget::Attachment& attachment) {
        VkClearValue value = {};
        if (attachment.type == RenderTarget::AttachmentType::Depth) {
            value.depthStencil = { clearDepth, clearStencil };
        } else {
            value.color = { { clearColor.r, clearColor.g, clearColor.b, clearColor.a } };
        }

        clearValues.push_back(value);
        if (attachment.multisampleResolveTexture)
            clearValues.push_back(value);
    });

    for (auto& [genAttachedTexture, requiredLayout] : renderTarget.attachedTextures) {
        auto& attachedTexture = static_cast<VulkanTexture&>(*genAttachedTexture);

        // We require textures that we render to to always have the optimal layout both as initial and final, so that we can
        // do things like LoadOp::Load and then just always assume that we have e.g. color target optimal.
        if (attachedTexture.currentLayout != requiredLayout) {

            VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            imageBarrier.oldLayout = attachedTexture.currentLayout;
            imageBarrier.newLayout = requiredLayout;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            imageBarrier.image = attachedTexture.image;
            imageBarrier.subresourceRange.aspectMask = attachedTexture.hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarrier.subresourceRange.baseMipLevel = 0;
            imageBarrier.subresourceRange.levelCount = attachedTexture.mipLevels();
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = attachedTexture.layerCount();

            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            vkCmdPipelineBarrier(m_commandBuffer,
                                 sourceStage, destinationStage, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
            attachedTexture.currentLayout = requiredLayout;
        }
    }

    // Explicitly transition the layouts of the sampled textures to an optimal layout (if it isn't already)
    {
        for (Texture* genTexture : renderState.sampledTextures) {
            auto& texture = static_cast<VulkanTexture&>(*genTexture);

            constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (texture.currentLayout != targetLayout) {

                VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                imageBarrier.oldLayout = texture.currentLayout;
                imageBarrier.newLayout = targetLayout;
                imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                imageBarrier.image = texture.image;
                imageBarrier.subresourceRange.aspectMask = texture.hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                imageBarrier.subresourceRange.baseMipLevel = 0;
                imageBarrier.subresourceRange.levelCount = texture.mipLevels();
                imageBarrier.subresourceRange.baseArrayLayer = 0;
                imageBarrier.subresourceRange.layerCount = texture.layerCount();

                VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                imageBarrier.srcAccessMask = 0;

                VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
                imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; // FIXME: Maybe VK_ACCESS_MEMORY_READ_BIT?

                vkCmdPipelineBarrier(m_commandBuffer,
                                     sourceStage, destinationStage, 0,
                                     0, nullptr,
                                     0, nullptr,
                                     1, &imageBarrier);
            }

            texture.currentLayout = targetLayout;
        }

        // TODO: We probably want to support storage images here as well!
        // for (const Texture* genTexture : renderState.storageImages) {}
    }

    VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    renderPassBeginInfo.renderPass = renderTarget.compatibleRenderPass;
    renderPassBeginInfo.framebuffer = renderTarget.framebuffer;

    auto& targetExtent = renderTarget.extent();
    renderPassBeginInfo.renderArea.offset = { 0, 0 };
    renderPassBeginInfo.renderArea.extent = { targetExtent.width(), targetExtent.height() };

    renderPassBeginInfo.clearValueCount = clearValues.size();
    renderPassBeginInfo.pClearValues = clearValues.data();

    // TODO: Handle subpasses properly!
    vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderState.pipeline);
}

void VulkanCommandList::endRendering()
{
    if (activeRenderState) {
        vkCmdEndRenderPass(m_commandBuffer);
        activeRenderState = nullptr;
    }
}

void VulkanCommandList::setRayTracingState(const RayTracingState& genRtState)
{
    if (!backend().hasRtxSupport()) {
        LogErrorAndExit("Trying to set ray tracing state but there is no ray tracing support!\n");
    }

    if (activeRenderState) {
        LogWarning("setRayTracingState: active render state when starting ray tracing.\n");
        endCurrentRenderPassIfAny();
    }

    auto& rtState = static_cast<const VulkanRayTracingState&>(genRtState);
    activeRayTracingState = &rtState;
    activeComputeState = nullptr;

    // Explicitly transition the layouts of the referenced textures to an optimal layout (if it isn't already)
    {
        for (Texture* texture : rtState.sampledTextures) {
            auto& vulkanTexture = static_cast<VulkanTexture&>(*texture);

            constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (vulkanTexture.currentLayout != targetLayout) {

                VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                imageBarrier.oldLayout = vulkanTexture.currentLayout;
                imageBarrier.newLayout = targetLayout;
                imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                imageBarrier.image = vulkanTexture.image;
                imageBarrier.subresourceRange.aspectMask = vulkanTexture.hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                imageBarrier.subresourceRange.baseMipLevel = 0;
                imageBarrier.subresourceRange.levelCount = vulkanTexture.mipLevels();
                imageBarrier.subresourceRange.baseArrayLayer = 0;
                imageBarrier.subresourceRange.layerCount = vulkanTexture.layerCount();

                VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                imageBarrier.srcAccessMask = 0;

                VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;
                imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; // FIXME: Maybe memory read & write?

                vkCmdPipelineBarrier(m_commandBuffer,
                                     sourceStage, destinationStage, 0,
                                     0, nullptr,
                                     0, nullptr,
                                     1, &imageBarrier);
                vulkanTexture.currentLayout = targetLayout;
            }
        }

        for (Texture* texture : rtState.storageImages) {
            auto& vulkanTexture = static_cast<VulkanTexture&>(*texture);

            constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_GENERAL;
            if (vulkanTexture.currentLayout != targetLayout) {

                VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                imageBarrier.oldLayout = vulkanTexture.currentLayout;
                imageBarrier.newLayout = targetLayout;
                imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                imageBarrier.image = vulkanTexture.image;
                imageBarrier.subresourceRange.aspectMask = vulkanTexture.hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                imageBarrier.subresourceRange.baseMipLevel = 0;
                imageBarrier.subresourceRange.levelCount = vulkanTexture.mipLevels();
                imageBarrier.subresourceRange.baseArrayLayer = 0;
                imageBarrier.subresourceRange.layerCount = vulkanTexture.layerCount();

                VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                imageBarrier.srcAccessMask = 0;

                VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;
                imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; // FIXME: Maybe memory read & write?

                vkCmdPipelineBarrier(m_commandBuffer,
                                     sourceStage, destinationStage, 0,
                                     0, nullptr,
                                     0, nullptr,
                                     1, &imageBarrier);
                vulkanTexture.currentLayout = targetLayout;
            }
        }
    }

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtState.pipeline);
}

void VulkanCommandList::setComputeState(const ComputeState& genComputeState)
{
    if (activeRenderState) {
        LogWarning("setComputeState: active render state when starting compute state.\n");
        endCurrentRenderPassIfAny();
    }

    auto& computeState = static_cast<const VulkanComputeState&>(genComputeState);
    activeComputeState = &computeState;
    activeRayTracingState = nullptr;

    // Explicitly transition the layouts of the referenced textures to an optimal layout (if it isn't already)
    {
        for (Texture* genTexture : computeState.sampledTextures) {
            auto& texture = static_cast<VulkanTexture&>(*genTexture);

            constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (texture.currentLayout != targetLayout) {

                VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                imageBarrier.oldLayout = texture.currentLayout;
                imageBarrier.newLayout = targetLayout;
                imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                imageBarrier.image = texture.image;
                imageBarrier.subresourceRange.aspectMask = texture.hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                imageBarrier.subresourceRange.baseMipLevel = 0;
                imageBarrier.subresourceRange.levelCount = texture.mipLevels();
                imageBarrier.subresourceRange.baseArrayLayer = 0;
                imageBarrier.subresourceRange.layerCount = texture.layerCount();

                VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                imageBarrier.srcAccessMask = 0;

                VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(m_commandBuffer,
                                     sourceStage, destinationStage, 0,
                                     0, nullptr,
                                     0, nullptr,
                                     1, &imageBarrier);
                texture.currentLayout = targetLayout;
            }
        }

        for (Texture* genTexture : computeState.storageImages) {
            auto& texture = static_cast<VulkanTexture&>(*genTexture);

            constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_GENERAL;
            if (texture.currentLayout != targetLayout) {

                VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                imageBarrier.oldLayout = texture.currentLayout;
                imageBarrier.newLayout = targetLayout;
                imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                imageBarrier.image = texture.image;
                imageBarrier.subresourceRange.aspectMask = texture.hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                imageBarrier.subresourceRange.baseMipLevel = 0;
                imageBarrier.subresourceRange.levelCount = texture.mipLevels();
                imageBarrier.subresourceRange.baseArrayLayer = 0;
                imageBarrier.subresourceRange.layerCount = texture.layerCount();

                VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                imageBarrier.srcAccessMask = 0;

                VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; // FIXME: Maybe memory read & write?

                vkCmdPipelineBarrier(m_commandBuffer,
                                     sourceStage, destinationStage, 0,
                                     0, nullptr,
                                     0, nullptr,
                                     1, &imageBarrier);
                texture.currentLayout = targetLayout;
            }
        }
    }

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computeState.pipeline);
}

void VulkanCommandList::bindSet(BindingSet& bindingSet, uint32_t index)
{
    if (!activeRenderState && !activeRayTracingState && !activeComputeState) {
        LogErrorAndExit("bindSet: no active render or compute or ray tracing state to bind to!\n");
    }

    ASSERT(!(activeRenderState && activeRayTracingState && activeComputeState));

    VkPipelineLayout pipelineLayout;
    VkPipelineBindPoint bindPoint;

    if (activeRenderState) {
        pipelineLayout = activeRenderState->pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
    if (activeComputeState) {
        pipelineLayout = activeComputeState->pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    }
    if (activeRayTracingState) {
        pipelineLayout = activeRayTracingState->pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_NV;
    }

    auto& vulkanBindingSet = static_cast<VulkanBindingSet&>(bindingSet);
    vkCmdBindDescriptorSets(m_commandBuffer, bindPoint, pipelineLayout, index, 1, &vulkanBindingSet.descriptorSet, 0, nullptr);
}

void VulkanCommandList::pushConstants(ShaderStage shaderStage, void* data, size_t size, size_t byteOffset)
{
    if (!activeRenderState && !activeRayTracingState && !activeComputeState) {
        LogErrorAndExit("pushConstants: no active render or compute or ray tracing state to bind to!\n");
    }

    ASSERT(!(activeRenderState && activeRayTracingState && activeComputeState));

    VkPipelineLayout pipelineLayout;
    if (activeRenderState) {
        pipelineLayout = activeRenderState->pipelineLayout;
    }
    if (activeComputeState) {
        pipelineLayout = activeComputeState->pipelineLayout;
    }
    if (activeRayTracingState) {
        pipelineLayout = activeRayTracingState->pipelineLayout;
    }

    // TODO: This isn't the only occurance of this shady table. We probably want a function for doing this translation!
    VkShaderStageFlags stageFlags = 0u;
    if (shaderStage & ShaderStageVertex)
        stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (shaderStage & ShaderStageFragment)
        stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (shaderStage & ShaderStageCompute)
        stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (shaderStage & ShaderStageRTRayGen)
        stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_NV;
    if (shaderStage & ShaderStageRTMiss)
        stageFlags |= VK_SHADER_STAGE_MISS_BIT_NV;
    if (shaderStage & ShaderStageRTClosestHit)
        stageFlags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;

    vkCmdPushConstants(m_commandBuffer, pipelineLayout, stageFlags, byteOffset, size, data);
}

void VulkanCommandList::draw(Buffer& vertexBuffer, uint32_t vertexCount)
{
    if (!activeRenderState) {
        LogErrorAndExit("draw: no active render state!\n");
    }

    VkBuffer vertBuffer = static_cast<VulkanBuffer&>(vertexBuffer).buffer;

    VkBuffer vertexBuffers[] = { vertBuffer };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(m_commandBuffer, vertexCount, 1, 0, 0);
}

void VulkanCommandList::drawIndexed(const Buffer& vertexBuffer, const Buffer& indexBuffer, uint32_t indexCount, IndexType indexType, uint32_t instanceIndex)
{
    if (!activeRenderState) {
        LogErrorAndExit("drawIndexed: no active render state!\n");
    }

    VkBuffer vertBuffer = static_cast<const VulkanBuffer&>(vertexBuffer).buffer;
    VkBuffer idxBuffer = static_cast<const VulkanBuffer&>(indexBuffer).buffer;

    VkBuffer vertexBuffers[] = { vertBuffer };
    VkDeviceSize offsets[] = { 0 };

    VkIndexType vkIndexType;
    switch (indexType) {
    case IndexType::UInt16:
        vkIndexType = VK_INDEX_TYPE_UINT16;
        break;
    case IndexType::UInt32:
        vkIndexType = VK_INDEX_TYPE_UINT32;
        break;
    }

    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(m_commandBuffer, idxBuffer, 0, vkIndexType);
    vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, 0, 0, instanceIndex);
}

void VulkanCommandList::rebuildTopLevelAcceratationStructure(TopLevelAS& tlas)
{
    if (!backend().hasRtxSupport())
        LogErrorAndExit("Trying to rebuild a top level acceleration structure but there is no ray tracing support!\n");

    auto& vulkanTlas = static_cast<VulkanTopLevelAS&>(tlas);

    // TODO: Maybe don't throw the allocation away (when building the first time), so we can reuse it here?
    //  However, it's a different size, though! So maybe not. Or if we use the max(build, rebuild) size?
    VmaAllocation scratchAllocation;
    VkBuffer scratchBuffer = backend().rtx().createScratchBufferForAccelerationStructure(vulkanTlas.accelerationStructure, true, scratchAllocation);

    VmaAllocation instanceAllocation;
    VkBuffer instanceBuffer = backend().rtx().createInstanceBuffer(tlas.instances(), instanceAllocation);

    VkAccelerationStructureInfoNV buildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
    buildInfo.instanceCount = tlas.instanceCount();
    buildInfo.geometryCount = 0;
    buildInfo.pGeometries = nullptr;

    m_backend.rtx().vkCmdBuildAccelerationStructureNV(
        m_commandBuffer,
        &buildInfo,
        instanceBuffer, 0,
        VK_TRUE,
        vulkanTlas.accelerationStructure,
        vulkanTlas.accelerationStructure,
        scratchBuffer, 0);

    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    vmaDestroyBuffer(m_backend.globalAllocator(), scratchBuffer, scratchAllocation);

    // Delete the old instance buffer & replace with the new one
    ASSERT(vulkanTlas.associatedBuffers.size() == 1);
    auto& [prevInstanceBuf, prevInstanceAlloc] = vulkanTlas.associatedBuffers[0];
    vmaDestroyBuffer(m_backend.globalAllocator(), prevInstanceBuf, prevInstanceAlloc);
    vulkanTlas.associatedBuffers[0] = { instanceBuffer, instanceAllocation };
}

void VulkanCommandList::traceRays(Extent2D extent)
{
    if (!activeRayTracingState)
        LogErrorAndExit("traceRays: no active ray tracing state!\n");
    if (!backend().hasRtxSupport())
        LogErrorAndExit("Trying to trace rays but there is no ray tracing support!\n");

    VkBuffer sbtBuffer = static_cast<const VulkanRayTracingState&>(*activeRayTracingState).sbtBuffer;

    uint32_t baseAlignment = backend().rtx().properties().shaderGroupBaseAlignment;

    uint32_t raygenOffset = 0; // we always start with raygen
    uint32_t raygenStride = baseAlignment; // since we have no data => TODO!
    size_t numRaygenShaders = 1; // for now, always just one

    uint32_t hitGroupOffset = raygenOffset + (numRaygenShaders * raygenStride);
    uint32_t hitGroupStride = baseAlignment; // since we have no data and a single shader for now => TODO! ALSO CONSIDER IF THIS SHOULD SIMPLY BE PASSED IN TO HERE?!
    size_t numHitGroups = activeRayTracingState->shaderBindingTable().hitGroups().size();

    uint32_t missOffset = hitGroupOffset + (numHitGroups * hitGroupStride);
    uint32_t missStride = baseAlignment; // since we have no data => TODO!

    backend().rtx().vkCmdTraceRaysNV(m_commandBuffer,
                                     sbtBuffer, raygenOffset,
                                     sbtBuffer, missOffset, missStride,
                                     sbtBuffer, hitGroupOffset, hitGroupStride,
                                     VK_NULL_HANDLE, 0, 0,
                                     extent.width(), extent.height(), 1);
}

void VulkanCommandList::dispatch(Extent3D globalSize, Extent3D localSize)
{
    uint32_t x = (globalSize.width() + localSize.width() - 1) / localSize.width();
    uint32_t y = (globalSize.height() + localSize.height() - 1) / localSize.height();
    uint32_t z = (globalSize.depth() + localSize.depth() - 1) / localSize.depth();
    dispatch(x, y, z);
}

void VulkanCommandList::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    if (!activeComputeState) {
        LogErrorAndExit("Trying to dispatch compute but there is no active compute state!\n");
    }
    vkCmdDispatch(m_commandBuffer, x, y, z);
}

void VulkanCommandList::waitEvent(uint8_t eventId, PipelineStage stage)
{
    VkEvent event = getEvent(eventId);
    VkPipelineStageFlags flags = stageFlags(stage);

    vkCmdWaitEvents(m_commandBuffer, 1, &event,
                    flags, flags, // TODO: Might be required that we have different stages here later!
                    0, nullptr,
                    0, nullptr,
                    0, nullptr);
}

void VulkanCommandList::resetEvent(uint8_t eventId, PipelineStage stage)
{
    VkEvent event = getEvent(eventId);
    vkCmdResetEvent(m_commandBuffer, event, stageFlags(stage));
}

void VulkanCommandList::signalEvent(uint8_t eventId, PipelineStage stage)
{
    VkEvent event = getEvent(eventId);
    vkCmdSetEvent(m_commandBuffer, event, stageFlags(stage));
}

void VulkanCommandList::slowBlockingReadFromBuffer(const Buffer& buffer, size_t offset, size_t size, void* dst)
{
    ASSERT(offset < buffer.size());
    ASSERT(size > 0);
    ASSERT(size <= buffer.size() - offset);

    auto& srcBuffer = static_cast<const VulkanBuffer&>(buffer);
    auto dstGenericBuffer = m_backend.createBuffer(buffer.size(), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::Readback);
    auto& dstBuffer = static_cast<VulkanBuffer&>(*dstGenericBuffer);

    m_backend.issueSingleTimeCommand([&](VkCommandBuffer cmdBuffer) {
        {
            VkBufferMemoryBarrier bufferMemoryBarrier { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };

            bufferMemoryBarrier.buffer = srcBuffer.buffer;
            bufferMemoryBarrier.offset = static_cast<VkDeviceSize>(offset);
            bufferMemoryBarrier.size = static_cast<VkDeviceSize>(size);

            bufferMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufferMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            bufferMemoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            bufferMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

            VkPipelineStageFlagBits srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            VkPipelineStageFlagBits dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;

            vkCmdPipelineBarrier(cmdBuffer,
                                 srcStageMask, dstStageMask,
                                 0,
                                 0, nullptr,
                                 1, &bufferMemoryBarrier,
                                 0, nullptr);
        }

        {
            VkBufferCopy bufferCopyRegion = {};
            bufferCopyRegion.size = size;
            bufferCopyRegion.srcOffset = offset;
            bufferCopyRegion.dstOffset = offset;

            vkCmdCopyBuffer(cmdBuffer, srcBuffer.buffer, dstBuffer.buffer, 1, &bufferCopyRegion);
        }

        {
            VkBufferMemoryBarrier bufferMemoryBarrier { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };

            bufferMemoryBarrier.buffer = dstBuffer.buffer;
            bufferMemoryBarrier.offset = static_cast<VkDeviceSize>(offset);
            bufferMemoryBarrier.size = static_cast<VkDeviceSize>(size);

            bufferMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufferMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            bufferMemoryBarrier.srcAccessMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            bufferMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_HOST_READ_BIT;

            VkPipelineStageFlagBits srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            VkPipelineStageFlagBits dstStageMask = VK_PIPELINE_STAGE_HOST_BIT;

            vkCmdPipelineBarrier(cmdBuffer,
                                 srcStageMask, dstStageMask,
                                 0,
                                 0, nullptr,
                                 1, &bufferMemoryBarrier,
                                 0, nullptr);
        }
    });

    VmaAllocator allocator = m_backend.globalAllocator();
    VmaAllocation allocation = dstBuffer.allocation;

    moos::u8* mappedBuffer;
    if (vmaMapMemory(allocator, allocation, (void**)&mappedBuffer) != VK_SUCCESS)
        LogError("Failed to map readback buffer memory...\n");
    vmaInvalidateAllocation(allocator, allocation, offset, size);

    std::memcpy(dst, mappedBuffer + offset, size);
    vmaUnmapMemory(allocator, allocation);
}

void VulkanCommandList::saveTextureToFile(const Texture& texture, const std::string& filePath)
{
    const VkFormat targetFormat = VK_FORMAT_R8G8B8A8_UNORM;

    auto& srcTex = static_cast<const VulkanTexture&>(texture);
    VkImageLayout prevSrcLayout = srcTex.currentLayout;
    VkImage srcImage = srcTex.image;

    VkImageCreateInfo imageCreateInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = targetFormat;
    imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageCreateInfo.extent.width = texture.extent().width();
    imageCreateInfo.extent.height = texture.extent().height();
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocCreateInfo {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    VkImage dstImage;
    VmaAllocation dstAllocation;
    VmaAllocationInfo dstAllocationInfo;
    if (vmaCreateImage(m_backend.globalAllocator(), &imageCreateInfo, &allocCreateInfo, &dstImage, &dstAllocation, &dstAllocationInfo) != VK_SUCCESS) {
        LogErrorAndExit("Failed to create temp image for screenshot\n");
    }

    bool success = m_backend.issueSingleTimeCommand([&](VkCommandBuffer cmdBuffer) {
        transitionImageLayoutDEBUG(dstImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);
        transitionImageLayoutDEBUG(srcImage, prevSrcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);

        VkImageCopy imageCopyRegion {};
        imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.srcSubresource.layerCount = 1;
        imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.dstSubresource.layerCount = 1; // FIXME: Maybe assert that the texture is not an array?
        imageCopyRegion.extent.width = texture.extent().width();
        imageCopyRegion.extent.height = texture.extent().height();
        imageCopyRegion.extent.depth = 1;

        vkCmdCopyImage(cmdBuffer,
                       srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &imageCopyRegion);

        // Transition destination image to general layout, which is the required layout for mapping the image memory
        transitionImageLayoutDEBUG(dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);
        transitionImageLayoutDEBUG(srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, prevSrcLayout, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);
    });

    if (!success) {
        LogError("Failed to setup screenshot image & data...\n");
    }

    // Get layout of the image (including row pitch/stride)
    VkImageSubresource subResource;
    subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subResource.mipLevel = 0;
    subResource.arrayLayer = 0;
    VkSubresourceLayout subResourceLayout;
    vkGetImageSubresourceLayout(device(), dstImage, &subResource, &subResourceLayout);

    char* data;
    vkMapMemory(device(), dstAllocationInfo.deviceMemory, 0, VK_WHOLE_SIZE, 0, (void**)&data);
    data += subResourceLayout.offset;

    bool shouldSwizzleRedAndBlue = (srcTex.vkFormat == VK_FORMAT_B8G8R8A8_SRGB) || (srcTex.vkFormat == VK_FORMAT_B8G8R8A8_UNORM) || (srcTex.vkFormat == VK_FORMAT_B8G8R8A8_SNORM);
    if (shouldSwizzleRedAndBlue) {
        int numPixels = texture.extent().width() * texture.extent().height();
        for (int i = 0; i < numPixels; ++i) {
            char tmp = data[4 * i + 0];
            data[4 * i + 0] = data[4 * i + 2];
            data[4 * i + 2] = tmp;
        }
    }

    if (!stbi_write_png(filePath.c_str(), texture.extent().width(), texture.extent().height(), 4, data, subResourceLayout.rowPitch)) {
        LogError("Failed to write screenshot to file...\n");
    }

    vkUnmapMemory(device(), dstAllocationInfo.deviceMemory);
    vmaDestroyImage(m_backend.globalAllocator(), dstImage, dstAllocation);
}

void VulkanCommandList::debugBarrier()
{
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    vkCmdPipelineBarrier(m_commandBuffer, sourceStage, destinationStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanCommandList::beginDebugLabel(const std::string& scopeName)
{
    if (!backend().hasDebugUtilsSupport())
        LogErrorAndExit("Trying to use debug utils stuff but there is no support!\n");

    VkDebugUtilsLabelEXT label { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
    label.pLabelName = scopeName.c_str();

    m_backend.debugUtils().vkCmdBeginDebugUtilsLabelEXT(m_commandBuffer, &label);
}

void VulkanCommandList::endDebugLabel()
{
    if (!backend().hasDebugUtilsSupport())
        LogErrorAndExit("Trying to use debug utils stuff but there is no support!\n");

    m_backend.debugUtils().vkCmdEndDebugUtilsLabelEXT(m_commandBuffer);
}

void VulkanCommandList::textureWriteBarrier(const Texture& genTexture)
{
    auto& texture = static_cast<const VulkanTexture&>(genTexture);

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = texture.image;

    // no layout transitions
    barrier.oldLayout = texture.currentLayout;
    barrier.newLayout = texture.currentLayout;

    // all texture writes must finish before any later memory access (r/w)
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    barrier.subresourceRange.aspectMask = texture.hasDepthFormat()
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = texture.layerCount();
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = texture.mipLevels();

    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
}

void VulkanCommandList::endNode(Badge<class VulkanBackend>)
{
    endCurrentRenderPassIfAny();
    debugBarrier(); // TODO: We probably don't need to do this..?
}

void VulkanCommandList::endCurrentRenderPassIfAny()
{
    if (activeRenderState) {
        vkCmdEndRenderPass(m_commandBuffer);
        activeRenderState = nullptr;
    }
}

VkEvent VulkanCommandList::getEvent(uint8_t eventId)
{
    const auto& events = m_backend.m_events;

    if (eventId >= events.size()) {
        LogErrorAndExit("Event of id %u requested, which is >= than the number of created events (%u)\n", eventId, events.size());
    }
    return events[eventId];
}

VkPipelineStageFlags VulkanCommandList::stageFlags(PipelineStage stage) const
{
    switch (stage) {
    case PipelineStage::Host:
        return VK_PIPELINE_STAGE_HOST_BIT;
    case PipelineStage::Compute:
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case PipelineStage::RayTracing:
        return VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;
    default:
        ASSERT_NOT_REACHED();
    }
}

void VulkanCommandList::transitionImageLayoutDEBUG(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags imageAspectMask, VkCommandBuffer commandBuffer) const
{
    VkImageMemoryBarrier imageMemoryBarrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

    imageMemoryBarrier.image = image;
    imageMemoryBarrier.oldLayout = oldLayout;
    imageMemoryBarrier.newLayout = newLayout;

    imageMemoryBarrier.subresourceRange.aspectMask = imageAspectMask;
    imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
    imageMemoryBarrier.subresourceRange.layerCount = 1;
    imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
    imageMemoryBarrier.subresourceRange.levelCount = 1;

    // Just do the strictest possible barrier so it should at least be valid, albeit slow.
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    VkPipelineStageFlagBits srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlagBits dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         srcStageMask, dstStageMask,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &imageMemoryBarrier);
}
