#include "VulkanCommandList.h"

#include "core/Logging.h"
#include "core/Types.h"
#include "VulkanBackend.h"
#include "VulkanResources.h"
#include "rendering/backend/vulkan/VulkanUpscalingState.h"
#include "utility/Profiling.h"
#include <fmt/format.h>
#include <stb_image_write.h>

// Shared shader headers
#include "shaders/shared/IndirectData.h"

VulkanCommandList::VulkanCommandList(VulkanBackend& backend, VkCommandBuffer commandBuffer)
    : m_backend(backend)
    , m_commandBuffer(commandBuffer)
{
}

void VulkanCommandList::fillBuffer(Buffer& genBuffer, u32 fillValue)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    auto& buffer = static_cast<VulkanBuffer&>(genBuffer);
    vkCmdFillBuffer(m_commandBuffer, buffer.buffer, 0, VK_WHOLE_SIZE, fillValue);
}

void VulkanCommandList::clearTexture(Texture& genTexture, ClearValue clearValue)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    auto& texture = static_cast<VulkanTexture&>(genTexture);

    VkImageAspectFlags aspectMask = 0u;
    if (texture.hasDepthFormat()) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (texture.hasStencilFormat()) {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    std::optional<VkImageLayout> originalLayout;
    if (texture.currentLayout != VK_IMAGE_LAYOUT_GENERAL && texture.currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        originalLayout = texture.currentLayout;

        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = originalLayout.value();
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = texture.image;
        imageBarrier.subresourceRange.aspectMask = aspectMask;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = texture.mipLevels();
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = texture.layerCount();

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

    VkImageSubresourceRange range {};
    range.aspectMask = aspectMask;
    range.baseMipLevel = 0;
    range.levelCount = texture.mipLevels();
    range.baseArrayLayer = 0;
    range.layerCount = texture.layerCount();

    if (texture.hasDepthFormat()) {
        VkClearDepthStencilValue clearDepthStencil {};
        clearDepthStencil.depth = clearValue.depth;
        clearDepthStencil.stencil = clearValue.stencil;

        vkCmdClearDepthStencilImage(m_commandBuffer, texture.image, VK_IMAGE_LAYOUT_GENERAL, &clearDepthStencil, 1, &range);

    } else {
        VkClearColorValue clearColor {};
        clearColor.float32[0] = clearValue.color.r;
        clearColor.float32[1] = clearValue.color.g;
        clearColor.float32[2] = clearValue.color.b;
        clearColor.float32[3] = clearValue.color.a;

        vkCmdClearColorImage(m_commandBuffer, texture.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
    }

    if (originalLayout.has_value() && originalLayout.value() != VK_IMAGE_LAYOUT_UNDEFINED && originalLayout.value() != VK_IMAGE_LAYOUT_PREINITIALIZED) {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.newLayout = originalLayout.value();
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = texture.image;
        imageBarrier.subresourceRange.aspectMask = aspectMask;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = texture.mipLevels();
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = texture.layerCount();

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

void VulkanCommandList::copyTexture(Texture& genSrc, Texture& genDst, uint32_t srcMip, uint32_t dstMip)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    auto& src = static_cast<VulkanTexture&>(genSrc);
    auto& dst = static_cast<VulkanTexture&>(genDst);

    ARKOSE_ASSERT(srcMip < src.mipLevels());
    ARKOSE_ASSERT(dstMip < dst.mipLevels());
    ARKOSE_ASSERT(src.hasDepthFormat() == dst.hasDepthFormat());
    ARKOSE_ASSERT(src.hasStencilFormat() == dst.hasStencilFormat());
    ARKOSE_ASSERT(src.aspectMask() == dst.aspectMask());
    VkImageAspectFlags aspectMask = src.aspectMask();

    ARKOSE_ASSERT(src.currentLayout != VK_IMAGE_LAYOUT_UNDEFINED && src.currentLayout != VK_IMAGE_LAYOUT_PREINITIALIZED);
    VkImageLayout initialSrcLayout = src.currentLayout;

    bool dstWasUndefined = false;
    VkImageLayout finalDstLayout = dst.currentLayout;
    if (finalDstLayout == VK_IMAGE_LAYOUT_UNDEFINED || finalDstLayout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
        finalDstLayout = VK_IMAGE_LAYOUT_GENERAL;
        dstWasUndefined = true;
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
        genBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

        std::array<VkImageMemoryBarrier, 2> barriers { genBarrier, genBarrier };

        barriers[0].image = src.image;
        barriers[0].oldLayout = src.currentLayout;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barriers[0].subresourceRange.baseMipLevel = srcMip;

        barriers[1].image = dst.image;
        barriers[1].oldLayout = dst.currentLayout;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        // If the initial is undefined we first transition *all* layers and mips to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        // so that we can then at the end copy all of them back to `finalDstLayout` and have no inconsistensies in that
        // final transition (i.e., all are the same).
        // TODO: Maybe also ensure we copy black/magenta/? pixels into the undefined layers & mips?
        if (dstWasUndefined) {
            barriers[1].subresourceRange.baseMipLevel = 0;
            barriers[1].subresourceRange.levelCount = dst.mipLevels();
            barriers[1].subresourceRange.baseArrayLayer = 0;
            barriers[1].subresourceRange.layerCount = dst.layerCount();
        } else {
            barriers[1].subresourceRange.baseMipLevel = dstMip;
        }

        vkCmdPipelineBarrier(m_commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             (uint32_t)barriers.size(), barriers.data());
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
        blit.srcOffsets[1] = extentToOffset(Extent3D(src.extentAtMip(srcMip)));
        blit.srcSubresource.aspectMask = aspectMask;
        blit.srcSubresource.mipLevel = srcMip;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = extentToOffset(Extent3D(dst.extentAtMip(dstMip)));
        blit.dstSubresource.aspectMask = aspectMask;
        blit.dstSubresource.mipLevel = dstMip;
        blit.dstSubresource.baseArrayLayer = 0;
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
        barriers[0].subresourceRange.baseMipLevel = srcMip;

        barriers[1].image = dst.image;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].newLayout = finalDstLayout;

        // Usually we just transition the specified layer and mip to what we need and then back, but we never want to
        // transition anything back to undefined. If the initial is undefined we transition *all* layers and mips to
        // the new layout so that we maintain our invariant that all of them should have the same layout always.
        if (dstWasUndefined) {
            barriers[1].subresourceRange.baseMipLevel = 0;
            barriers[1].subresourceRange.levelCount = dst.mipLevels();
            barriers[1].subresourceRange.baseArrayLayer = 0;
            barriers[1].subresourceRange.layerCount = dst.layerCount();
        } else {
            barriers[1].subresourceRange.baseMipLevel = dstMip;
        }

        vkCmdPipelineBarrier(m_commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             (uint32_t)barriers.size(), barriers.data());

        dst.currentLayout = finalDstLayout;
    }
}

void VulkanCommandList::generateMipmaps(Texture& genTexture)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    beginDebugLabel(fmt::format("Generate Mipmaps ({}x{})", genTexture.extent().width(), genTexture.extent().height()));

    auto& texture = static_cast<VulkanTexture&>(genTexture);

    if (!texture.hasMipmaps()) {
        ARKOSE_LOG(Error, "generateMipmaps called on command list for texture which doesn't have space for mipmaps allocated. Ignoring request.");
        return;
    }

    if (texture.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        ARKOSE_LOG(Error, "generateMipmaps called on command list for texture which currently has the layout VK_IMAGE_LAYOUT_UNDEFINED. Ignoring request.");
        return;
    }

    // Make sure that all mips have whatever layout the texture has before this function was called!
    VkImageLayout finalLayout = texture.currentLayout;

    VkImageAspectFlags aspectMask = texture.aspectMask();

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

    endDebugLabel();
}

void VulkanCommandList::executeBufferCopyOperations(std::vector<BufferCopyOperation> copyOperations)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (copyOperations.size() == 0)
        return;

    beginDebugLabel(fmt::format("Execute buffer copy operations (x{})", copyOperations.size()));

    std::vector<VkBufferMemoryBarrier> bufferMemoryBarriers {};
    for (const BufferCopyOperation& copyOperation : copyOperations) {

        if (copyOperation.size == 0)
            continue;

        if (std::holds_alternative<BufferCopyOperation::BufferDestination>(copyOperation.destination)) {
            auto const& copyDestination = std::get<BufferCopyOperation::BufferDestination>(copyOperation.destination);

            VkBufferCopy bufferCopyRegion = {};
            bufferCopyRegion.size = copyOperation.size;
            bufferCopyRegion.srcOffset = copyOperation.srcOffset;
            bufferCopyRegion.dstOffset = copyDestination.offset;

            auto srcVkBuffer = static_cast<VulkanBuffer*>(copyOperation.srcBuffer)->buffer;
            auto dstVkBuffer = static_cast<VulkanBuffer*>(copyDestination.buffer)->buffer;

            vkCmdCopyBuffer(m_commandBuffer, srcVkBuffer, dstVkBuffer, 1, &bufferCopyRegion);

            VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            barrier.buffer = dstVkBuffer;
            barrier.size = copyOperation.size;
            barrier.offset = copyDestination.offset;

            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            bufferMemoryBarriers.push_back(barrier);

        } else if (std::holds_alternative<BufferCopyOperation::TextureDestination>(copyOperation.destination)) {
            auto const& copyDestination = std::get<BufferCopyOperation::TextureDestination>(copyOperation.destination);
            VulkanTexture& dstTexture = *static_cast<VulkanTexture*>(copyDestination.texture);

            // Ensure that the *entire* texture is in the correct layout
            if (dstTexture.currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                imageBarrier.image = dstTexture.image;
                imageBarrier.subresourceRange.aspectMask = dstTexture.aspectMask();
                imageBarrier.subresourceRange.baseMipLevel = 0;
                imageBarrier.subresourceRange.levelCount = dstTexture.mipLevels();
                imageBarrier.subresourceRange.baseArrayLayer = 0;
                imageBarrier.subresourceRange.layerCount = dstTexture.layerCount();

                VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                imageBarrier.srcAccessMask = 0;

                VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                vkCmdPipelineBarrier(m_commandBuffer, sourceStage, destinationStage, 0,
                                     0, nullptr,
                                     0, nullptr,
                                     1, &imageBarrier);

                dstTexture.currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            }

            VkBufferImageCopy copyRegion = {};

            copyRegion.bufferOffset = copyOperation.srcOffset;

            // (zeros here indicate tightly packed data)
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;

            Extent3D mipExtent = dstTexture.extent3DAtMip(narrow_cast<u32>(copyDestination.textureMip));

            copyRegion.imageOffset = VkOffset3D { 0, 0, 0 };
            copyRegion.imageExtent = VkExtent3D { mipExtent.width(),
                                                  mipExtent.height(),
                                                  mipExtent.depth() };

            copyRegion.imageSubresource.aspectMask = dstTexture.aspectMask();
            copyRegion.imageSubresource.mipLevel = narrow_cast<u32>(copyDestination.textureMip);
            copyRegion.imageSubresource.baseArrayLayer = narrow_cast<u32>(copyDestination.textureArrayLayer);
            copyRegion.imageSubresource.layerCount = 1; // TODO: For now, just one at a time

            auto srcVkBuffer = static_cast<VulkanBuffer*>(copyOperation.srcBuffer)->buffer;
            auto dstVkImage = dstTexture.image;

            vkCmdCopyBufferToImage(m_commandBuffer, srcVkBuffer, dstVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        } else {
            ASSERT_NOT_REACHED();
        }
    }

    if (bufferMemoryBarriers.size() > 0) {
        vkCmdPipelineBarrier(m_commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                             0, nullptr,
                             (uint32_t)bufferMemoryBarriers.size(), bufferMemoryBarriers.data(),
                             0, nullptr);
    }

    endDebugLabel();
}

void VulkanCommandList::beginRendering(const RenderState& genRenderState, bool autoSetViewport)
{
    if (activeRenderState) {
        ARKOSE_LOG(Warning, "setRenderState: already active render state!");
        endCurrentRenderPassIfAny();
    }

    genRenderState.renderTarget().forEachAttachmentInOrder([](const RenderTarget::Attachment& attachment) {
        if (attachment.loadOp == LoadOp::Clear) {
            ARKOSE_LOG(Fatal, "CommandList: calling beginRendering (with no extra arguments) for rendering to a render target with LoadOp::Clear textures. "
                            "For these render targets always use beginRendering with clear colors etc. specified. Exiting!");
        }
    });

    beginRendering(genRenderState, ClearValue(), autoSetViewport);
}

void VulkanCommandList::beginRendering(const RenderState& genRenderState, ClearValue clearValue, bool autoSetViewport)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (activeRenderState) {
        ARKOSE_LOG(Warning, "setRenderState: already active render state!");
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
            value.depthStencil.depth = clearValue.depth;
            value.depthStencil.stencil = clearValue.stencil;
        } else {
            value.color = { { clearValue.color.r, clearValue.color.g, clearValue.color.b, clearValue.color.a } };
        }

        clearValues.push_back(value);
        if (attachment.multisampleResolveTexture)
            clearValues.push_back(value);
    });

    // TODO: What about imageless framebuffer? Then I guess we would want to transition those images instead? Or just assume they are already of the correct layout?
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
            imageBarrier.subresourceRange.aspectMask = attachedTexture.aspectMask();
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

    // Explicitly transition the layouts of the referenced textures to an optimal layout (if it isn't already)
    std::vector<VkImageMemoryBarrier> imageMemoryBarriers {};
    renderState.stateBindings().forEachBinding([&](const ShaderBinding& bindingInfo) {
        if (bindingInfo.type() == ShaderBindingType::SampledTexture) {
            for (Texture const* texture : bindingInfo.getSampledTextures()) {
                auto& vulkanTexture = static_cast<VulkanTexture const&>(*texture);

                constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (vulkanTexture.currentLayout != targetLayout) {

                    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    imageBarrier.oldLayout = vulkanTexture.currentLayout;
                    imageBarrier.newLayout = targetLayout;
                    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                    imageBarrier.image = vulkanTexture.image;
                    imageBarrier.subresourceRange.aspectMask = vulkanTexture.aspectMask();
                    imageBarrier.subresourceRange.baseMipLevel = 0;
                    imageBarrier.subresourceRange.levelCount = vulkanTexture.mipLevels();
                    imageBarrier.subresourceRange.baseArrayLayer = 0;
                    imageBarrier.subresourceRange.layerCount = vulkanTexture.layerCount();

                    imageBarrier.srcAccessMask = 0;
                    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                    imageMemoryBarriers.push_back(imageBarrier);

                    vulkanTexture.currentLayout = targetLayout;
                }
            }
        } else if (bindingInfo.type() == ShaderBindingType::StorageTexture) {
            for (TextureMipView textureMip : bindingInfo.getStorageTextures()) {
                auto& vulkanTexture = static_cast<VulkanTexture&>(textureMip.texture());

                constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_GENERAL;
                if (vulkanTexture.currentLayout != targetLayout) {

                    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    imageBarrier.oldLayout = vulkanTexture.currentLayout;
                    imageBarrier.newLayout = targetLayout;
                    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                    // NOTE: We always transition all mips so we can ensure our invariant of same layout accross mips holds.
                    imageBarrier.image = vulkanTexture.image;
                    imageBarrier.subresourceRange.aspectMask = vulkanTexture.aspectMask();
                    imageBarrier.subresourceRange.baseMipLevel = 0;
                    imageBarrier.subresourceRange.levelCount = vulkanTexture.mipLevels();
                    imageBarrier.subresourceRange.baseArrayLayer = 0;
                    imageBarrier.subresourceRange.layerCount = vulkanTexture.layerCount();

                    imageBarrier.srcAccessMask = 0;
                    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

                    imageMemoryBarriers.push_back(imageBarrier);

                    vulkanTexture.currentLayout = targetLayout;
                }
            }
        }
    });

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    vkCmdPipelineBarrier(m_commandBuffer,
                         sourceStage, destinationStage, 0,
                         0, nullptr,
                         0, nullptr,
                         (uint32_t)imageMemoryBarriers.size(), imageMemoryBarriers.data());

    VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    renderPassBeginInfo.renderPass = renderTarget.compatibleRenderPass;
    renderPassBeginInfo.framebuffer = renderTarget.framebuffer;

    auto& targetExtent = renderTarget.extent();
    renderPassBeginInfo.renderArea.offset = { 0, 0 };
    renderPassBeginInfo.renderArea.extent = { targetExtent.width(), targetExtent.height() };

    renderPassBeginInfo.clearValueCount = (uint32_t)clearValues.size();
    renderPassBeginInfo.pClearValues = clearValues.data();

    VkRenderPassAttachmentBeginInfo attachmentBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO };
    if (renderTarget.framebufferIsImageless) {

        ARKOSE_ASSERT(renderTarget.totalAttachmentCount() == renderTarget.imagelessFramebufferAttachments.size());
        attachmentBeginInfo.attachmentCount = (uint32_t)renderTarget.imagelessFramebufferAttachments.size();
        attachmentBeginInfo.pAttachments = renderTarget.imagelessFramebufferAttachments.data();

        renderPassBeginInfo.pNext = &attachmentBeginInfo;
    }

    // TODO: Handle subpasses properly!
    vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderState.pipeline);

    renderState.stateBindings().forEachBindingSet([this](u32 setIndex, BindingSet& bindingSet) {
        bindSet(bindingSet, setIndex);
    });

    if (autoSetViewport) {
        setViewport({ 0, 0 }, renderTarget.extent().asIntVector());
    }
}

void VulkanCommandList::endRendering()
{
    if (activeRenderState) {
        vkCmdEndRenderPass(m_commandBuffer);
        activeRenderState = nullptr;
    }
}

void VulkanCommandList::setRayTracingState(const RayTracingState& rtState)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!backend().hasRayTracingSupport()) {
        ARKOSE_LOG(Fatal, "Trying to set ray tracing state but there is no ray tracing support!");
    }

    if (activeRenderState) {
        ARKOSE_LOG(Warning, "setRayTracingState: active render state when starting ray tracing.");
        endCurrentRenderPassIfAny();
    }

    activeRayTracingState = &rtState;
    activeComputeState = nullptr;

    // Explicitly transition the layouts of the referenced textures to an optimal layout (if it isn't already)
    std::vector<VkImageMemoryBarrier> imageMemoryBarriers {};
    rtState.stateBindings().forEachBinding([&](const ShaderBinding& bindingInfo) {
        if (bindingInfo.type() == ShaderBindingType::SampledTexture) {
            for (Texture const* texture : bindingInfo.getSampledTextures()) {
                auto& vulkanTexture = static_cast<VulkanTexture const&>(*texture);

                constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (vulkanTexture.currentLayout != targetLayout) {

                    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    imageBarrier.oldLayout = vulkanTexture.currentLayout;
                    imageBarrier.newLayout = targetLayout;
                    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                    imageBarrier.image = vulkanTexture.image;
                    imageBarrier.subresourceRange.aspectMask = vulkanTexture.aspectMask();
                    imageBarrier.subresourceRange.baseMipLevel = 0;
                    imageBarrier.subresourceRange.levelCount = vulkanTexture.mipLevels();
                    imageBarrier.subresourceRange.baseArrayLayer = 0;
                    imageBarrier.subresourceRange.layerCount = vulkanTexture.layerCount();

                    imageBarrier.srcAccessMask = 0;
                    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                    imageMemoryBarriers.push_back(imageBarrier);

                    vulkanTexture.currentLayout = targetLayout;
                }
            }
        } else if (bindingInfo.type() == ShaderBindingType::StorageTexture) {
            for (TextureMipView textureMip : bindingInfo.getStorageTextures()) {
                auto& vulkanTexture = static_cast<VulkanTexture&>(textureMip.texture());

                constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_GENERAL;
                if (vulkanTexture.currentLayout != targetLayout) {

                    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    imageBarrier.oldLayout = vulkanTexture.currentLayout;
                    imageBarrier.newLayout = targetLayout;
                    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                    // NOTE: We always transition all mips so we can ensure our invariant of same layout accross mips holds.
                    imageBarrier.image = vulkanTexture.image;
                    imageBarrier.subresourceRange.aspectMask = vulkanTexture.aspectMask();
                    imageBarrier.subresourceRange.baseMipLevel = 0;
                    imageBarrier.subresourceRange.levelCount = vulkanTexture.mipLevels();
                    imageBarrier.subresourceRange.baseArrayLayer = 0;
                    imageBarrier.subresourceRange.layerCount = vulkanTexture.layerCount();

                    imageBarrier.srcAccessMask = 0;
                    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

                    imageMemoryBarriers.push_back(imageBarrier);

                    vulkanTexture.currentLayout = targetLayout;
                }
            }
        }
    });

    auto issuePipelineBarrierForRayTracingStateResources = [&](VkPipelineStageFlags destinationStage) {
        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        vkCmdPipelineBarrier(m_commandBuffer,
                             sourceStage, destinationStage, 0,
                             0, nullptr,
                             0, nullptr,
                             (uint32_t)imageMemoryBarriers.size(), imageMemoryBarriers.data());
    };

    switch (backend().rayTracingBackend()) {
    case VulkanBackend::RayTracingBackend::NvExtension: {
        auto& rtxRtState = static_cast<const VulkanRayTracingStateNV&>(rtState);
        issuePipelineBarrierForRayTracingStateResources(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV);
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtxRtState.pipeline);
    } break;
    case VulkanBackend::RayTracingBackend::KhrExtension: {
        auto& khrRtState = static_cast<const VulkanRayTracingStateKHR&>(rtState);
        issuePipelineBarrierForRayTracingStateResources(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, khrRtState.pipeline);
    } break;
    }

    rtState.stateBindings().forEachBindingSet([this](u32 setIndex, BindingSet& bindingSet) {
        bindSet(bindingSet, setIndex);
    });
}

void VulkanCommandList::setComputeState(const ComputeState& genComputeState)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (activeRenderState) {
        ARKOSE_LOG(Warning, "setComputeState: active render state when starting compute state.");
        endCurrentRenderPassIfAny();
    }

    auto& computeState = static_cast<const VulkanComputeState&>(genComputeState);
    activeComputeState = &computeState;
    activeRayTracingState = nullptr;

    // Explicitly transition the layouts of the referenced textures to an optimal layout (if it isn't already)
    std::vector<VkImageMemoryBarrier> imageMemoryBarriers {};
    computeState.stateBindings().forEachBinding([&](ShaderBinding const& bindingInfo) {
        if (bindingInfo.type() == ShaderBindingType::SampledTexture) {
            for (Texture const* texture : bindingInfo.getSampledTextures()) {
                auto& vulkanTexture = static_cast<VulkanTexture const&>(*texture);

                constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (vulkanTexture.currentLayout != targetLayout) {

                    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    imageBarrier.oldLayout = vulkanTexture.currentLayout;
                    imageBarrier.newLayout = targetLayout;
                    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                    imageBarrier.image = vulkanTexture.image;
                    imageBarrier.subresourceRange.aspectMask = vulkanTexture.aspectMask();
                    imageBarrier.subresourceRange.baseMipLevel = 0;
                    imageBarrier.subresourceRange.levelCount = vulkanTexture.mipLevels();
                    imageBarrier.subresourceRange.baseArrayLayer = 0;
                    imageBarrier.subresourceRange.layerCount = vulkanTexture.layerCount();

                    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    imageBarrier.srcAccessMask = 0;

                    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                    imageMemoryBarriers.push_back(imageBarrier);
                    vulkanTexture.currentLayout = targetLayout;
                }
            }
        } else if (bindingInfo.type() == ShaderBindingType::StorageTexture) {
            for (TextureMipView textureMip : bindingInfo.getStorageTextures()) {
                auto& vulkanTexture = static_cast<VulkanTexture&>(textureMip.texture());

                constexpr VkImageLayout targetLayout = VK_IMAGE_LAYOUT_GENERAL;
                if (vulkanTexture.currentLayout != targetLayout) {

                    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    imageBarrier.oldLayout = vulkanTexture.currentLayout;
                    imageBarrier.newLayout = targetLayout;
                    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

                    // NOTE: We always transition all mips so we can ensure our invariant of same layout accross mips holds.
                    imageBarrier.image = vulkanTexture.image;
                    imageBarrier.subresourceRange.aspectMask = vulkanTexture.aspectMask();
                    imageBarrier.subresourceRange.baseMipLevel = 0;
                    imageBarrier.subresourceRange.levelCount = vulkanTexture.mipLevels();
                    imageBarrier.subresourceRange.baseArrayLayer = 0;
                    imageBarrier.subresourceRange.layerCount = vulkanTexture.layerCount();

                    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    imageBarrier.srcAccessMask = 0;

                    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; // FIXME: Maybe memory read & write?

                    imageMemoryBarriers.push_back(imageBarrier);
                    vulkanTexture.currentLayout = targetLayout;
                }
            }
        }
    });

    if (imageMemoryBarriers.size() > 0) {
        vkCmdPipelineBarrier(m_commandBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             narrow_cast<u32>(imageMemoryBarriers.size()), imageMemoryBarriers.data());
    }

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computeState.pipeline);

    computeState.stateBindings().forEachBindingSet([this](u32 setIndex, BindingSet& bindingSet) {
        bindSet(bindingSet, setIndex);
    });
}

void VulkanCommandList::evaluateUpscaling(UpscalingState const& upscalingState, UpscalingParameters parameters)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    auto const& vulkanUpscalingState = static_cast<VulkanUpscalingState const&>(upscalingState);

#if WITH_DLSS
    if (upscalingState.upscalingTech() == UpscalingTech::DLSS) {
        backend().dlssFeature().evaluate(m_commandBuffer, vulkanUpscalingState.dlssFeatureHandle, parameters);
    } else {
#else
    {
#endif
        ASSERT_NOT_REACHED();
    }
}

void VulkanCommandList::bindSet(BindingSet& bindingSet, u32 index)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!activeRenderState && !activeRayTracingState && !activeComputeState) {
        ARKOSE_LOG(Fatal, "bindSet: no active render or compute or ray tracing state to bind to!");
    }

    ARKOSE_ASSERT(!(activeRenderState && activeRayTracingState && activeComputeState));

    auto pipelinePair = getCurrentlyBoundPipelineLayout();
    VkPipelineLayout pipelineLayout = pipelinePair.first;
    VkPipelineBindPoint bindPoint = pipelinePair.second;

    auto& vulkanBindingSet = static_cast<VulkanBindingSet&>(bindingSet);
    vkCmdBindDescriptorSets(m_commandBuffer, bindPoint, pipelineLayout, index, 1, &vulkanBindingSet.descriptorSet, 0, nullptr);
}

void VulkanCommandList::bindTextureSet(BindingSet& bindingSet, u32 index)
{
    // Ensure we only have sampled textures in the set, and that they are all available to be read from
    for (ShaderBinding const& shaderBinding : bindingSet.shaderBindings()) {
        ARKOSE_ASSERT(shaderBinding.type() == ShaderBindingType::SampledTexture);

        // NOTE: I don't think this is strictly needed, as all layouts are technically valid to sample from(?)
        // However, it might be good to ensure that they are in a GOOD layout to be sampled as well..
        // auto const* vulkanTexture = static_cast<VulkanTexture const*>(shaderBinding.getSampledTexture());
        // ARKOSE_ASSERT(vulkanTexture->currentLayout == VK_IMAGE_LAYOUT_GENERAL
        //              || vulkanTexture->currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        //              || vulkanTexture->currentLayout == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
        //              || vulkanTexture->currentLayout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
        //              || vulkanTexture->currentLayout == VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL
        //              || vulkanTexture->currentLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        //              || vulkanTexture->currentLayout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL);
    }

    bindSet(bindingSet, index);
}

void VulkanCommandList::setNamedUniform(const std::string& name, void* data, size_t size)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    requireExactlyOneStateToBeSet("setNamedUniform");

    Shader const& shader = getCurrentlyBoundShader();

    // TODO: Don't do it lazily like this
    if (!shader.hasUniformBindingsSetup()) {

        std::unordered_map<std::string, Shader::UniformBinding> bindings;

        const std::vector<VulkanBackend::PushConstantInfo>& pushConstants = m_backend.identifyAllPushConstants(shader);
        for (auto& constant : pushConstants) {

            Shader::UniformBinding binding;
            binding.stages = constant.stages;
            binding.offset = constant.offset;
            binding.size = constant.size;

            bindings[constant.name] = binding;
        }

        const_cast<Shader&>(shader).setUniformBindings(bindings);
    }

    std::optional<Shader::UniformBinding> binding = shader.uniformBindingForName(name);
    if (binding.has_value()) {
        if (size != binding->size) {
            ARKOSE_LOG(Fatal, "setNamedUniform: size mismatch for uniform named '{}' (provided={}, actual={}).", name, size, binding->size);
        }

        VkPipelineLayout pipelineLayout = getCurrentlyBoundPipelineLayout().first;
        VkShaderStageFlags stageFlags = backend().shaderStageToVulkanShaderStageFlags(binding->stages);
        vkCmdPushConstants(m_commandBuffer, pipelineLayout, stageFlags, binding->offset, binding->size, data);

    } else {
        ARKOSE_LOG(Error, "setNamedUniform: no corresponding uniform for name '{}', ignoring.", name);
    }
}

void VulkanCommandList::draw(u32 vertexCount, u32 firstVertex)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!activeRenderState) {
        ARKOSE_LOG(Fatal, "draw: no active render state!");
    }
    if (m_boundVertexBuffer == VK_NULL_HANDLE) {
        ARKOSE_LOG(Fatal, "draw: no bound vertex buffer!");
    }

    vkCmdDraw(m_commandBuffer, vertexCount, 1, firstVertex, 0);
}

void VulkanCommandList::drawIndexed(u32 indexCount, u32 instanceIndex)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!activeRenderState) {
        ARKOSE_LOG(Fatal, "drawIndexed: no active render state!");
    }
    if (m_boundVertexBuffer == VK_NULL_HANDLE) {
        ARKOSE_LOG(Fatal, "drawIndexed: no bound vertex buffer!");
    }
    if (m_boundIndexBuffer == VK_NULL_HANDLE) {
        ARKOSE_LOG(Fatal, "drawIndexed: no bound index buffer!");
    }

    vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, 0, 0, instanceIndex);
}

void VulkanCommandList::drawIndirect(const Buffer& indirectBuffer, const Buffer& countBuffer)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!activeRenderState) {
        ARKOSE_LOG(Fatal, "drawIndirect: no active render state!");
    }
    if (!m_boundVertexBuffer) {
        ARKOSE_LOG(Fatal, "drawIndirect: no bound vertex buffer!");
    }
    if (!m_boundIndexBuffer) {
        ARKOSE_LOG(Fatal, "drawIndirect: no bound index buffer!");
    }

    if (indirectBuffer.usage() != Buffer::Usage::IndirectBuffer) {
        ARKOSE_LOG(Fatal, "drawIndirect: supplied indirect buffer is not an indirect buffer!");
    }
    if (countBuffer.usage() != Buffer::Usage::IndirectBuffer) {
        ARKOSE_LOG(Fatal, "drawIndirect: supplied count buffer is not an indirect buffer!");
    }

    VkBuffer vulkanIndirectBuffer = static_cast<const VulkanBuffer&>(indirectBuffer).buffer;
    VkBuffer vulkanCountBuffer = static_cast<const VulkanBuffer&>(countBuffer).buffer;

    // TODO: Parameterize these maybe? Now we assume that they are packed etc.
    uint32_t indirectDataStride = sizeof(IndexedDrawCmd);
    uint32_t maxDrawCount = (uint32_t)indirectBuffer.size() / indirectDataStride;

    vkCmdDrawIndexedIndirectCount(m_commandBuffer, vulkanIndirectBuffer, 0u, vulkanCountBuffer, 0u, maxDrawCount, indirectDataStride);
}

void VulkanCommandList::drawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!backend().hasMeshShadingSupport()) {
        ARKOSE_LOG(Fatal, "Trying to draw mesh tasks but there is no mesh shading support!");
    }

    if (!activeRenderState) {
        ARKOSE_LOG(Fatal, "drawMeshTasks: no active render state!");
    }

    backend().meshShaderEXT().vkCmdDrawMeshTasksEXT(m_commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandList::drawMeshTasksIndirect(Buffer const& indirectBuffer, u32 indirectDataStride, u32 indirectDataOffset,
                                              Buffer const& countBuffer, u32 countDataOffset)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!backend().hasMeshShadingSupport()) {
        ARKOSE_LOG(Fatal, "Trying to draw mesh tasks but there is no mesh shading support!");
    }

    if (!activeRenderState) {
        ARKOSE_LOG(Fatal, "drawMeshTasksIndirect: no active render state!");
    }

    if (indirectBuffer.usage() != Buffer::Usage::IndirectBuffer) {
        ARKOSE_LOG(Fatal, "drawMeshTasksIndirect: supplied indirect buffer is not an indirect buffer!");
    }
    if (countBuffer.usage() != Buffer::Usage::IndirectBuffer) {
        ARKOSE_LOG(Fatal, "drawMeshTasksIndirect: supplied count buffer is not an indirect buffer!");
    }

    VkBuffer vulkanIndirectBuffer = static_cast<const VulkanBuffer&>(indirectBuffer).buffer;
    VkBuffer vulkanCountBuffer = static_cast<const VulkanBuffer&>(countBuffer).buffer;

    ARKOSE_ASSERT(indirectDataStride >= 3 * sizeof(u32));
    u32 maxDrawCount = narrow_cast<u32>(indirectBuffer.size() - indirectDataOffset) / indirectDataStride;

    backend().meshShaderEXT().vkCmdDrawMeshTasksIndirectCountEXT(m_commandBuffer,
                                                                 vulkanIndirectBuffer, indirectDataOffset,
                                                                 vulkanCountBuffer, countDataOffset,
                                                                 maxDrawCount, indirectDataStride);
}

void VulkanCommandList::setViewport(ivec2 origin, ivec2 size)
{
    ARKOSE_ASSERT(origin.x >= 0);
    ARKOSE_ASSERT(origin.y >= 0);
    ARKOSE_ASSERT(size.x > 0);
    ARKOSE_ASSERT(size.x > 0);

    VkViewport viewport = {};
    viewport.x = static_cast<float>(origin.x);
    viewport.y = static_cast<float>(origin.y);
    viewport.width = static_cast<float>(size.x);
    viewport.height = static_cast<float>(size.y);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // TODO: Allow independent scissor control
    VkRect2D scissorRect = {};
    scissorRect.offset = { origin.x, origin.y };
    scissorRect.extent.width = size.x;
    scissorRect.extent.height = size.y;

    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissorRect);
}

void VulkanCommandList::setDepthBias(float constantFactor, float slopeFactor)
{
    // If the depthBiasClamp feature is not enabled, depthBiasClamp must be 0.0
    constexpr float depthBiasClamp = 0.0f;

    vkCmdSetDepthBias(m_commandBuffer, constantFactor, depthBiasClamp, slopeFactor);
}

void VulkanCommandList::bindVertexBuffer(Buffer const& vertexBuffer, u32 stride, u32 bindingIdx)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (vertexBuffer.usage() != Buffer::Usage::Vertex)
        ARKOSE_LOG(Fatal, "bindVertexBuffer: not a vertex buffer!");

    VkBuffer vulkanBuffer = static_cast<const VulkanBuffer&>(vertexBuffer).buffer;
    if (m_boundVertexBuffer == vulkanBuffer)
        return;

    VkBuffer vertexBuffers[] = { vulkanBuffer };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(m_commandBuffer, bindingIdx, 1, vertexBuffers, offsets);
    m_boundVertexBuffer = vulkanBuffer;
}

void VulkanCommandList::bindIndexBuffer(Buffer const& indexBuffer, IndexType indexType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (indexBuffer.usage() != Buffer::Usage::Index)
        ARKOSE_LOG(Fatal, "bindIndexBuffer: not an index buffer!");

    VkBuffer vulkanBuffer = static_cast<const VulkanBuffer&>(indexBuffer).buffer;
    if (m_boundIndexBuffer == vulkanBuffer) {
        return;
    }

    VkIndexType vulkanIndexType;
    switch (indexType) {
    case IndexType::UInt16:
        vulkanIndexType = VK_INDEX_TYPE_UINT16;
        break;
    case IndexType::UInt32:
        vulkanIndexType = VK_INDEX_TYPE_UINT32;
        break;
    default:
        ASSERT_NOT_REACHED();
        break;
    }

    
    vkCmdBindIndexBuffer(m_commandBuffer, vulkanBuffer, 0, vulkanIndexType);

    m_boundIndexBuffer = vulkanBuffer;
}

void VulkanCommandList::issueDrawCall(const DrawCallDescription& drawCall)
{
    //SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!activeRenderState)
        ARKOSE_LOG(Fatal, "issueDrawCall: no active render state!");

    VulkanBuffer const* vertexBuffer = static_cast<const VulkanBuffer*>(drawCall.vertexBuffer);
    if (vertexBuffer && vertexBuffer->buffer != m_boundVertexBuffer)
        ARKOSE_LOG(Fatal, "issueDrawCall: bind the correct vertex buffer before calling this!");
    VulkanBuffer const* indexBuffer = static_cast<const VulkanBuffer*>(drawCall.indexBuffer);
    if (indexBuffer && indexBuffer->buffer != m_boundIndexBuffer)
        ARKOSE_LOG(Fatal, "issueDrawCall: bind the correct index buffer before calling this!");

    ARKOSE_ASSERT(drawCall.instanceCount > 0);

    switch (drawCall.type) {
    case DrawCallDescription::Type::NonIndexed:
        vkCmdDraw(m_commandBuffer, drawCall.vertexCount, drawCall.instanceCount, drawCall.firstVertex, drawCall.firstInstance);
        break;
    case DrawCallDescription::Type::Indexed:
        vkCmdDrawIndexed(m_commandBuffer, drawCall.indexCount, drawCall.instanceCount, drawCall.firstIndex, drawCall.vertexOffset, drawCall.firstInstance);
        break;
    }
}

void VulkanCommandList::buildTopLevelAcceratationStructure(TopLevelAS& tlas, AccelerationStructureBuildType buildType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!backend().hasRayTracingSupport())
        ARKOSE_LOG(Fatal, "Trying to rebuild a top level acceleration structure but there is no ray tracing support!");

    beginDebugLabel("Rebuild TLAS");

    switch (backend().rayTracingBackend()) {
    case VulkanBackend::RayTracingBackend::KhrExtension: {
        auto& khrTlas = static_cast<VulkanTopLevelASKHR&>(tlas);
        khrTlas.build(m_commandBuffer, buildType);
    } break;
    case VulkanBackend::RayTracingBackend::NvExtension: {
        auto& rtxTlas = static_cast<VulkanTopLevelASNV&>(tlas);
        rtxTlas.build(m_commandBuffer, buildType);
    } break;
    }

    endDebugLabel();
}

void VulkanCommandList::buildBottomLevelAcceratationStructure(BottomLevelAS& blas, AccelerationStructureBuildType buildType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!backend().hasRayTracingSupport())
        ARKOSE_LOG(Fatal, "Trying to rebuild a bottom level acceleration structure but there is no ray tracing support!");

    beginDebugLabel("Rebuild BLAS");

    switch (backend().rayTracingBackend()) {
    case VulkanBackend::RayTracingBackend::KhrExtension: {
        auto& khrBlas = static_cast<VulkanBottomLevelASKHR&>(blas);
        khrBlas.build(m_commandBuffer, buildType);
    } break;
    case VulkanBackend::RayTracingBackend::NvExtension: {
        [[maybe_unused]] auto& rtxBlas = static_cast<VulkanBottomLevelASNV&>(blas);
        NOT_YET_IMPLEMENTED();
    } break;
    }

    endDebugLabel();
}

void VulkanCommandList::traceRays(Extent2D extent)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!activeRayTracingState)
        ARKOSE_LOG(Fatal, "traceRays: no active ray tracing state!");
    if (!backend().hasRayTracingSupport())
        ARKOSE_LOG(Fatal, "Trying to trace rays but there is no ray tracing support!");

    switch (backend().rayTracingBackend()) {
    case VulkanBackend::RayTracingBackend::KhrExtension: {
        auto& khrRtState = static_cast<const VulkanRayTracingStateKHR&>(*activeRayTracingState);
        khrRtState.traceRaysWithShaderOnlySBT(m_commandBuffer, extent);
    } break;
    case VulkanBackend::RayTracingBackend::NvExtension: {
        auto& rtxRtState = static_cast<const VulkanRayTracingStateNV&>(*activeRayTracingState);
        rtxRtState.traceRays(m_commandBuffer, extent);
    } break;
    }
}

void VulkanCommandList::dispatch(Extent3D globalSize, Extent3D localSize)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    uint32_t x = (globalSize.width() + localSize.width() - 1) / localSize.width();
    uint32_t y = (globalSize.height() + localSize.height() - 1) / localSize.height();
    uint32_t z = (globalSize.depth() + localSize.depth() - 1) / localSize.depth();
    dispatch(x, y, z);
}

void VulkanCommandList::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!activeComputeState) {
        ARKOSE_LOG(Fatal, "Trying to dispatch compute but there is no active compute state!");
    }
    vkCmdDispatch(m_commandBuffer, x, y, z);
}

void VulkanCommandList::slowBlockingReadFromBuffer(const Buffer& buffer, size_t offset, size_t size, void* dst)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    ARKOSE_ASSERT(offset < buffer.size());
    ARKOSE_ASSERT(size > 0);
    ARKOSE_ASSERT(size <= buffer.size() - offset);

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

    ark::u8* mappedBuffer;
    if (vmaMapMemory(allocator, allocation, (void**)&mappedBuffer) != VK_SUCCESS)
        ARKOSE_LOG(Error, "Failed to map readback buffer memory...");
    vmaInvalidateAllocation(allocator, allocation, offset, size);

    std::memcpy(dst, mappedBuffer + offset, size);
    vmaUnmapMemory(allocator, allocation);
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
#if defined(TRACY_ENABLE)
    auto tracyScope = std::make_unique<tracy::VkCtxScope>(backend().tracyVulkanContext(),
                                                          TracyLine,
                                                          TracyFile, strlen(TracyFile),
                                                          TracyFunction, strlen(TracyFunction),
                                                          scopeName.c_str(), scopeName.size(),
                                                          m_commandBuffer, true);
    m_tracyDebugLabelStack.push_back(std::move(tracyScope));
#endif

    if (!backend().hasDebugUtilsSupport())
        return;

    VkDebugUtilsLabelEXT label { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
    label.pLabelName = scopeName.c_str();

    m_backend.debugUtils().vkCmdBeginDebugUtilsLabelEXT(m_commandBuffer, &label);
}

void VulkanCommandList::endDebugLabel()
{
#if defined(TRACY_ENABLE)
    ARKOSE_ASSERT(m_tracyDebugLabelStack.size() > 0);
    m_tracyDebugLabelStack.pop_back();
#endif

    if (!backend().hasDebugUtilsSupport())
        return;

    m_backend.debugUtils().vkCmdEndDebugUtilsLabelEXT(m_commandBuffer);
}

void VulkanCommandList::textureWriteBarrier(const Texture& genTexture)
{
    auto& texture = static_cast<const VulkanTexture&>(genTexture);

    if (texture.currentLayout == VK_IMAGE_LAYOUT_PREINITIALIZED || texture.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        // Texture has no valid data written to it, so this barrier can be a no-op
        return;
    }

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = texture.image;

    // no layout transitions
    barrier.oldLayout = texture.currentLayout;
    barrier.newLayout = texture.currentLayout;

    // all texture writes must finish before any later memory access (r/w)
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    barrier.subresourceRange.aspectMask = texture.aspectMask();
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

void VulkanCommandList::textureMipWriteBarrier(const Texture& genTexture, uint32_t mip)
{
    auto& texture = static_cast<const VulkanTexture&>(genTexture);

    if (texture.currentLayout == VK_IMAGE_LAYOUT_PREINITIALIZED || texture.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        // Texture has no valid data written to it, so this barrier can be a no-op
        return;
    }

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = texture.image;

    // no layout transitions
    barrier.oldLayout = texture.currentLayout;
    barrier.newLayout = texture.currentLayout;

    // all texture writes must finish before any later memory access (r/w)
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    barrier.subresourceRange.aspectMask = texture.aspectMask();
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = texture.layerCount();
    barrier.subresourceRange.baseMipLevel = mip;
    barrier.subresourceRange.levelCount = 1;

    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
}

void VulkanCommandList::bufferWriteBarrier(std::vector<Buffer const*> buffers)
{
    if (buffers.size() == 0)
        return;

    std::vector<VkBufferMemoryBarrier> barriers {};
    barriers.resize(buffers.size());

    for (int i = 0; i < buffers.size(); ++i) {
        Buffer const& buffer = *buffers[i];

        VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        barrier.buffer = static_cast<VulkanBuffer const&>(buffer).buffer;

        // the whole range
        barrier.offset = 0;
        barrier.size = buffer.size();

        // all writes must finish before any later memory access (r/w)
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        barriers[i] = barrier;
    }

    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0,
                         0, nullptr,
                         (uint32_t)barriers.size(), barriers.data(),
                         0, nullptr);
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

void VulkanCommandList::requireExactlyOneStateToBeSet(const std::string& context) const
{
    if (!activeRenderState && !activeRayTracingState && !activeComputeState) {
        ARKOSE_LOG(Fatal, "{}: no active render or compute or ray tracing state to bind to!", context);
    }

    ARKOSE_ASSERT(!(activeRenderState && activeRayTracingState && activeComputeState));
}

std::pair<VkPipelineLayout, VkPipelineBindPoint> VulkanCommandList::getCurrentlyBoundPipelineLayout()
{
    if (activeRenderState) {
        return { activeRenderState->pipelineLayout, VK_PIPELINE_BIND_POINT_GRAPHICS };
    }
    if (activeComputeState) {
        return { activeComputeState->pipelineLayout, VK_PIPELINE_BIND_POINT_COMPUTE };
    }
    if (activeRayTracingState) {
        switch (backend().rayTracingBackend()) {
        case VulkanBackend::RayTracingBackend::NvExtension:
            return { static_cast<const VulkanRayTracingStateNV*>(activeRayTracingState)->pipelineLayout, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV };
        case VulkanBackend::RayTracingBackend::KhrExtension:
            return { static_cast<const VulkanRayTracingStateKHR*>(activeRayTracingState)->pipelineLayout, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR };
        }
    }

    ASSERT_NOT_REACHED();
}

const Shader& VulkanCommandList::getCurrentlyBoundShader()
{
    if (activeRenderState) {
        return activeRenderState->shader();
    }
    if (activeComputeState) {
        return activeComputeState->shader();
    }
    if (activeRayTracingState) {
        return activeRayTracingState->shaderBindingTable().pseudoShader();
    }

    ASSERT_NOT_REACHED();
}
