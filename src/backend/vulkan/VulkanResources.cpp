#include "VulkanResources.h"

#include "backend/vulkan/VulkanBackend.h"
#include "rendering/ShaderManager.h"
#include "utility/CapList.h"
#include "utility/Logging.h"
#include <mooslib/core.h>
#include <stb_image.h>

VulkanBuffer::VulkanBuffer(Backend& backend, size_t size, Usage usage, MemoryHint memoryHint)
    : Buffer(backend, size, usage, memoryHint)
{
    // NOTE: Vulkan doesn't seem to like to create buffers of size 0. Of course, it's correct
    //  in that it is stupid, but it can be useful when debugging and testing to just not supply
    //  any data and create an empty buffer while not having to change any shader code or similar.
    //  To get around this here we simply force a size of 1 instead, but as far as the frontend
    //  is conserned we don't have access to that one byte.
    size_t bufferSize = size;
    if (bufferSize == 0) {
        bufferSize = 1;
    }

    VkBufferUsageFlags usageFlags = 0u;
    switch (usage) {
    case Buffer::Usage::Vertex:
        usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        break;
    case Buffer::Usage::Index:
        usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        break;
    case Buffer::Usage::UniformBuffer:
        usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        break;
    case Buffer::Usage::StorageBuffer:
        usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    if (vulkanDebugMode) {
        // for nsight debugging & similar stuff)
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo allocCreateInfo = {};
    switch (memoryHint) {
    case Buffer::MemoryHint::GpuOnly:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        break;
    case Buffer::MemoryHint::GpuOptimal:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case Buffer::MemoryHint::TransferOptimal:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU; // (ensures host visible!)
        allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case Buffer::MemoryHint::Readback:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU; // (ensures host visible!)
        allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    }

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.size = bufferSize;
    bufferCreateInfo.usage = usageFlags;

    // TODO: Add a to<VulkanBackend> utility, or something like that
    auto& allocator = dynamic_cast<VulkanBackend&>(backend).globalAllocator();

    VmaAllocationInfo allocationInfo;
    if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &buffer, &allocation, &allocationInfo) != VK_SUCCESS) {
        LogErrorAndExit("Could not create buffer of size %u.\n", size);
    }
}

VulkanBuffer::~VulkanBuffer()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), buffer, allocation);
}

void VulkanBuffer::updateData(const std::byte* data, size_t updateSize)
{
    if (updateSize == 0)
        return;
    if (updateSize > size())
        LogErrorAndExit("Attempt at updating buffer outside of bounds!\n");

    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());

    switch (memoryHint()) {
    case Buffer::MemoryHint::GpuOptimal:
        if (!vulkanBackend.setBufferDataUsingStagingBuffer(buffer, data, updateSize)) {
            LogError("Could not update the data of GPU-optimal buffer\n");
        }
        break;
    case Buffer::MemoryHint::TransferOptimal:
        if (!vulkanBackend.setBufferMemoryUsingMapping(allocation, data, updateSize)) {
            LogError("Could not update the data of transfer-optimal buffer\n");
        }
        break;
    case Buffer::MemoryHint::GpuOnly:
        LogError("Can't update buffer with GpuOnly memory hint, ignoring\n");
        break;
    }
}

VulkanTexture::VulkanTexture(Backend& backend, Extent2D extent, Format format, MinFilter minFilter, MagFilter magFilter, Mipmap mip, Multisampling ms)
    : Texture(backend, extent, format, minFilter, magFilter, mip, ms)
{
    // HACK: Now we longer specify what usage we want for the texture, and instead always select all
    //  possible capabilities. However, some texture formats (e.g. sRGB formats) do not support being
    //  used as a storage image, so we need to explicitly disable it for those formats.
    bool storageCapable = true;

    switch (format) {
    case Texture::Format::R32:
        vkFormat = VK_FORMAT_R32_UINT;
        break;
    case Texture::Format::RGBA8:
        vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    case Texture::Format::sRGBA8:
        vkFormat = VK_FORMAT_R8G8B8A8_SRGB;
        storageCapable = false;
        break;
    case Texture::Format::R16F:
        vkFormat = VK_FORMAT_R16_SFLOAT;
        break;
    case Texture::Format::RGBA16F:
        vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        break;
    case Texture::Format::RGBA32F:
        vkFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
        break;
    case Texture::Format::Depth32F:
        vkFormat = VK_FORMAT_D32_SFLOAT;
        storageCapable = false;
        break;
    case Texture::Format::Unknown:
        LogErrorAndExit("Trying to create new texture with format Unknown, which is not allowed!\n");
    default:
        ASSERT_NOT_REACHED();
    }

    // Since we don't specify usage we have to assume all of them may be used (at least the common operations)
    const VkImageUsageFlags attachmentFlags = hasDepthFormat() ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageUsageFlags usageFlags = attachmentFlags | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (storageCapable)
        usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;

    // (if we later want to generate mipmaps we need the ability to use each mip as a src & dst in blitting)
    if (hasMipmaps()) {
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if (vulkanDebugMode) {
        // for nsight debugging & similar stuff, which needs access to everything
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    // TODO: For now always keep images in device local memory.
    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.extent = { .width = extent.width(), .height = extent.height(), .depth = 1 };
    imageCreateInfo.mipLevels = mipLevels();
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.usage = usageFlags;
    imageCreateInfo.format = vkFormat;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.samples = static_cast<VkSampleCountFlagBits>(multisampling());
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    auto& allocator = static_cast<VulkanBackend&>(backend).globalAllocator();
    if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        LogError("VulkanBackend::newTexture(): could not create image.\n");
    }

    VkImageAspectFlags aspectFlags = 0u;
    if (hasDepthFormat()) {
        aspectFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        aspectFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkImageViewCreateInfo viewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCreateInfo.subresourceRange.aspectMask = aspectFlags;
    viewCreateInfo.image = image;
    viewCreateInfo.format = vkFormat;
    viewCreateInfo.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY
    };
    viewCreateInfo.subresourceRange.baseMipLevel = 0;
    viewCreateInfo.subresourceRange.levelCount = mipLevels();
    viewCreateInfo.subresourceRange.baseArrayLayer = 0;
    viewCreateInfo.subresourceRange.layerCount = 1;

    VkDevice device = static_cast<VulkanBackend&>(backend).device();
    if (vkCreateImageView(device, &viewCreateInfo, nullptr, &imageView) != VK_SUCCESS) {
        LogError("VulkanBackend::newTexture(): could not create image view.\n");
    }

    VkFilter vkMinFilter;
    switch (minFilter) {
    case Texture::MinFilter::Linear:
        vkMinFilter = VK_FILTER_LINEAR;
        break;
    case Texture::MinFilter::Nearest:
        vkMinFilter = VK_FILTER_NEAREST;
        break;
    }

    VkFilter vkMagFilter;
    switch (magFilter) {
    case Texture::MagFilter::Linear:
        vkMagFilter = VK_FILTER_LINEAR;
        break;
    case Texture::MagFilter::Nearest:
        vkMagFilter = VK_FILTER_NEAREST;
        break;
    }

    VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    samplerCreateInfo.magFilter = vkMagFilter;
    samplerCreateInfo.minFilter = vkMinFilter;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCreateInfo.anisotropyEnable = VK_TRUE;
    samplerCreateInfo.maxAnisotropy = 16.0f;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.minLod = 0.0f;
    switch (mipmap()) {
    case Texture::Mipmap::None:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerCreateInfo.maxLod = 0.0f;
        break;
    case Texture::Mipmap::Nearest:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerCreateInfo.maxLod = mipLevels();
        break;
    case Texture::Mipmap::Linear:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.maxLod = mipLevels();
        break;
    }

    if (vkCreateSampler(device, &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS) {
        LogError("VulkanBackend::newTexture(): could not create sampler for the image.\n");
    }

    currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

VulkanTexture::~VulkanTexture()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());
    vkDestroySampler(vulkanBackend.device(), sampler, nullptr);
    vkDestroyImageView(vulkanBackend.device(), imageView, nullptr);
    vmaDestroyImage(vulkanBackend.globalAllocator(), image, allocation);
}

void VulkanTexture::setPixelData(vec4 pixel)
{
    int numChannels;
    bool isHdr = false;

    switch (format()) {
    case Texture::Format::R16F:
        numChannels = 1;
        isHdr = true;
        break;
    case Texture::Format::RGBA8:
    case Texture::Format::sRGBA8:
        numChannels = 4;
        isHdr = false;
        break;
    case Texture::Format::RGBA16F:
    case Texture::Format::RGBA32F:
        numChannels = 4;
        isHdr = true;
        break;
    case Texture::Format::Depth32F:
        numChannels = 1;
        isHdr = true;
        break;
    case Texture::Format::Unknown:
        ASSERT_NOT_REACHED();
        break;
    }

    ASSERT(numChannels == 4);

    moos::u8 pixels[4];
    VkDeviceSize pixelsSize;

    if (isHdr) {
        pixelsSize = sizeof(vec4);
    } else {
        pixels[0] = (stbi_uc)(moos::clamp(pixel.x, 0.0f, 1.0f) * 255.99f);
        pixels[1] = (stbi_uc)(moos::clamp(pixel.y, 0.0f, 1.0f) * 255.99f);
        pixels[2] = (stbi_uc)(moos::clamp(pixel.z, 0.0f, 1.0f) * 255.99f);
        pixels[3] = (stbi_uc)(moos::clamp(pixel.w, 0.0f, 1.0f) * 255.99f);
        pixelsSize = 4 * sizeof(stbi_uc);
    }

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.size = pixelsSize;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    if (vmaCreateBuffer(vulkanBackend.globalAllocator(), &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        LogError("Could not create staging buffer for updating image with pixel-data.\n");
    }

    if (!vulkanBackend.setBufferMemoryUsingMapping(stagingAllocation, isHdr ? (void*)value_ptr(pixel) : (void*)pixels, pixelsSize)) {
        LogError("Could not set the buffer memory for the staging buffer for updating image with pixel-data.\n");
        return;
    }

    AT_SCOPE_EXIT([&]() {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), stagingBuffer, stagingAllocation);
    });

    // NOTE: Since we are updating the texture we don't care what was in the image before. For these cases undefined
    //  works fine, since it will simply discard/ignore whatever data is in it before.
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!vulkanBackend.transitionImageLayout(image, hasDepthFormat(), oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        LogError("Could not transition the image to transfer layout.\n");
        return;
    }
    if (!vulkanBackend.copyBufferToImage(stagingBuffer, image, 1, 1, hasDepthFormat())) {
        LogError("Could not copy the staging buffer to the image.\n");
        return;
    }

    VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    {
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

        imageBarrier.srcAccessMask = 0;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }

    bool success = static_cast<VulkanBackend&>(backend()).issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &imageBarrier);
    });

    if (!success) {
        LogError("Error transitioning layout after setting pixel data\n");
    }

    currentLayout = VK_IMAGE_LAYOUT_GENERAL;
}

void VulkanTexture::setData(const std::byte* data, size_t size)
{
    ASSERT(!hasFloatingPointDataFormat());

    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.size = size;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    if (vmaCreateBuffer(vulkanBackend.globalAllocator(), &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        LogError("VulkanBackend::updateTexture(): could not create staging buffer.\n");
    }

    if (!vulkanBackend.setBufferMemoryUsingMapping(stagingAllocation, (void*)data, size)) {
        LogError("VulkanBackend::updateTexture(): could set the buffer memory for the staging buffer.\n");
        return;
    }

    AT_SCOPE_EXIT([&]() {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), stagingBuffer, stagingAllocation);
    });

    // NOTE: Since we are updating the texture we don't care what was in the image before. For these cases undefined
    //  works fine, since it will simply discard/ignore whatever data is in it before.
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!vulkanBackend.transitionImageLayout(image, hasDepthFormat(), oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        LogError("Could not transition the image to transfer layout.\n");
        return;
    }
    if (!vulkanBackend.copyBufferToImage(stagingBuffer, image, extent().width(), extent().height(), hasDepthFormat())) {
        LogError("Could not copy the staging buffer to the image.\n");
        return;
    }

    currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (mipmap() != Texture::Mipmap::None && extent().width() > 1 && extent().height() > 1) {
        vulkanBackend.generateMipmaps(*this, VK_IMAGE_LAYOUT_GENERAL);
    } else {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        {
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            imageBarrier.image = image;
            imageBarrier.subresourceRange.aspectMask = hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarrier.subresourceRange.baseMipLevel = 0;
            imageBarrier.subresourceRange.levelCount = 1;
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = 1;

            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

            imageBarrier.srcAccessMask = 0;
            imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        }

        bool success = static_cast<VulkanBackend&>(backend()).issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        });

        if (!success) {
            LogError("Error transitioning layout after setting texture data\n");
        }
    }
    currentLayout = VK_IMAGE_LAYOUT_GENERAL;
}

void VulkanTexture::setData(const float* data, size_t size)
{
    ASSERT(hasFloatingPointDataFormat());

    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.size = size;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    if (vmaCreateBuffer(vulkanBackend.globalAllocator(), &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        LogError("VulkanBackend::updateTexture(): could not create staging buffer.\n");
    }

    if (!vulkanBackend.setBufferMemoryUsingMapping(stagingAllocation, (void*)data, size)) {
        LogError("VulkanBackend::updateTexture(): could set the buffer memory for the staging buffer.\n");
        return;
    }

    AT_SCOPE_EXIT([&]() {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), stagingBuffer, stagingAllocation);
    });

    // NOTE: Since we are updating the texture we don't care what was in the image before. For these cases undefined
    //  works fine, since it will simply discard/ignore whatever data is in it before.
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!vulkanBackend.transitionImageLayout(image, hasDepthFormat(), oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        LogError("Could not transition the image to transfer layout.\n");
        return;
    }
    if (!vulkanBackend.copyBufferToImage(stagingBuffer, image, extent().width(), extent().height(), hasDepthFormat())) {
        LogError("Could not copy the staging buffer to the image.\n");
        return;
    }

    currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (mipmap() != Texture::Mipmap::None && extent().width() > 1 && extent().height() > 1) {
        vulkanBackend.generateMipmaps(*this, VK_IMAGE_LAYOUT_GENERAL);
    } else {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        {
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            imageBarrier.image = image;
            imageBarrier.subresourceRange.aspectMask = hasDepthFormat() ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            imageBarrier.subresourceRange.baseMipLevel = 0;
            imageBarrier.subresourceRange.levelCount = 1;
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = 1;

            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

            imageBarrier.srcAccessMask = 0;
            imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        }

        bool success = static_cast<VulkanBackend&>(backend()).issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        });

        if (!success) {
            LogError("Error transitioning layout after setting texture data\n");
        }
    }
    currentLayout = VK_IMAGE_LAYOUT_GENERAL;
}

VulkanRenderTarget::VulkanRenderTarget(Backend& backend, std::vector<Attachment> attachments)
    : RenderTarget(backend, std::move(attachments))
{
    std::vector<VkImageView> allAttachmentImageViews {};
    std::vector<VkAttachmentDescription> allAttachments {};
    std::vector<VkAttachmentReference> colorAttachmentRefs {};
    std::optional<VkAttachmentReference> depthAttachmentRef {};

    for (auto& [type, genTexture, loadOp, storeOp] : sortedAttachments()) {

        // If the attachments are sorted properly (i.e. depth very last) then this should never happen!
        // This is important for the VkAttachmentReference attachment index later in this loop.
        ASSERT(!depthAttachmentRef.has_value());

        // FIXME: Add is<Type> and to<Type> utilities!
        MOOSLIB_ASSERT(genTexture);
        auto& texture = dynamic_cast<VulkanTexture&>(*genTexture);

        VkAttachmentDescription attachment = {};
        attachment.format = texture.vkFormat;

        // TODO: Handle multisampling and stencil stuff!
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        switch (loadOp) {
        case LoadOp::Load:
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

            // TODO/FIXME: For LOAD_OP_LOAD we actually need to provide a valid initialLayout! Using texInfo.currentLayout
            //  won't work since we only use the layout at the time of creating this render pass, and not what it is in
            //  runtime. Not sure what the best way of doing this is. What about always using explicit transitions before
            //  binding this render target, and then here have the same initialLayout and finalLayout so nothing(?) happens.
            //  Could maybe work, but we have to figure out if it's actually a noop if initial & final are equal!
            attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; //texInfo.currentLayout;
            ASSERT_NOT_REACHED();
            break;
        case LoadOp::Clear:
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
        }

        switch (storeOp) {
        case StoreOp::Store:
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            break;
        case StoreOp::Ignore:
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            break;
        }

        if (type == RenderTarget::AttachmentType::Depth) {
            attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        } else {
            attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        uint32_t attachmentIndex = allAttachments.size();
        allAttachments.push_back(attachment);
        allAttachmentImageViews.push_back(texture.imageView);

        VkAttachmentReference attachmentRef = {};
        attachmentRef.attachment = attachmentIndex;
        attachmentRef.layout = attachment.finalLayout;
        if (type == RenderTarget::AttachmentType::Depth) {
            depthAttachmentRef = attachmentRef;
        } else {
            colorAttachmentRefs.push_back(attachmentRef);
        }
    }

    // TODO: How do we want to support multiple subpasses in the future?
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = colorAttachmentRefs.size();
    subpass.pColorAttachments = colorAttachmentRefs.data();
    if (depthAttachmentRef.has_value()) {
        subpass.pDepthStencilAttachment = &depthAttachmentRef.value();
    }

    VkRenderPassCreateInfo renderPassCreateInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassCreateInfo.attachmentCount = allAttachments.size();
    renderPassCreateInfo.pAttachments = allAttachments.data();
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpass;
    renderPassCreateInfo.dependencyCount = 0;
    renderPassCreateInfo.pDependencies = nullptr;

    // TODO: Add a to<VulkanBackend> utility, or something like that
    VkDevice device = dynamic_cast<VulkanBackend&>(backend).device();

    if (vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &compatibleRenderPass) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create render pass\n");
    }

    VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebufferCreateInfo.renderPass = compatibleRenderPass;
    framebufferCreateInfo.attachmentCount = allAttachmentImageViews.size();
    framebufferCreateInfo.pAttachments = allAttachmentImageViews.data();
    framebufferCreateInfo.width = extent().width();
    framebufferCreateInfo.height = extent().height();
    framebufferCreateInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create framebuffer\n");
    }

    for (auto& attachment : sortedAttachments()) {
        VkImageLayout finalLayout = (attachment.type == RenderTarget::AttachmentType::Depth)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachedTextures.push_back({ attachment.texture, finalLayout });
    }
}

VulkanRenderTarget::~VulkanRenderTarget()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());
    vkDestroyFramebuffer(vulkanBackend.device(), framebuffer, nullptr);
    vkDestroyRenderPass(vulkanBackend.device(), compatibleRenderPass, nullptr);
}

VulkanBindingSet::VulkanBindingSet(Backend& backend, std::vector<ShaderBinding> bindings)
    : BindingSet(backend, std::move(bindings))
{
    const auto& device = dynamic_cast<VulkanBackend&>(backend).device();

    {
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings {};
        layoutBindings.reserve(shaderBindings().size());

        for (auto& bindingInfo : shaderBindings()) {

            VkDescriptorSetLayoutBinding binding = {};
            binding.binding = bindingInfo.bindingIndex;
            binding.descriptorCount = bindingInfo.count;

            switch (bindingInfo.type) {
            case ShaderBindingType::UniformBuffer:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case ShaderBindingType::StorageBuffer:
            case ShaderBindingType::StorageBufferArray:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case ShaderBindingType::StorageImage:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;
            case ShaderBindingType::TextureSampler:
            case ShaderBindingType::TextureSamplerArray:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                break;
            case ShaderBindingType::RTAccelerationStructure:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
                break;
            default:
                ASSERT_NOT_REACHED();
            }

            if (bindingInfo.shaderStage & ShaderStageVertex)
                binding.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
            if (bindingInfo.shaderStage & ShaderStageFragment)
                binding.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (bindingInfo.shaderStage & ShaderStageCompute)
                binding.stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
            if (bindingInfo.shaderStage & ShaderStageRTRayGen)
                binding.stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_NV;
            if (bindingInfo.shaderStage & ShaderStageRTMiss)
                binding.stageFlags |= VK_SHADER_STAGE_MISS_BIT_NV;
            if (bindingInfo.shaderStage & ShaderStageRTClosestHit)
                binding.stageFlags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
            if (bindingInfo.shaderStage & ShaderStageRTAnyHit)
                binding.stageFlags |= VK_SHADER_STAGE_ANY_HIT_BIT_NV;
            if (bindingInfo.shaderStage & ShaderStageRTIntersection)
                binding.stageFlags |= VK_SHADER_STAGE_INTERSECTION_BIT_NV;

            ASSERT(binding.stageFlags != 0);

            binding.pImmutableSamplers = nullptr;

            layoutBindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptorSetLayoutCreateInfo.bindingCount = layoutBindings.size();
        descriptorSetLayoutCreateInfo.pBindings = layoutBindings.data();

        if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set layout\n");
        }
    }

    {
        // TODO: Maybe in the future we don't want one pool per shader binding state? We could group a lot of stuff together probably..?

        std::unordered_map<ShaderBindingType, size_t> bindingTypeIndex {};
        std::vector<VkDescriptorPoolSize> descriptorPoolSizes {};

        for (auto& bindingInfo : shaderBindings()) {

            ShaderBindingType type = bindingInfo.type;

            auto entry = bindingTypeIndex.find(type);
            if (entry == bindingTypeIndex.end()) {

                VkDescriptorPoolSize poolSize = {};
                poolSize.descriptorCount = bindingInfo.count;

                switch (bindingInfo.type) {
                case ShaderBindingType::UniformBuffer:
                    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    break;
                case ShaderBindingType::StorageBuffer:
                case ShaderBindingType::StorageBufferArray:
                    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    break;
                case ShaderBindingType::StorageImage:
                    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    break;
                case ShaderBindingType::TextureSampler:
                case ShaderBindingType::TextureSamplerArray:
                    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    break;
                case ShaderBindingType::RTAccelerationStructure:
                    poolSize.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
                    break;
                default:
                    ASSERT_NOT_REACHED();
                }

                bindingTypeIndex[type] = descriptorPoolSizes.size();
                descriptorPoolSizes.push_back(poolSize);

            } else {

                size_t index = entry->second;
                VkDescriptorPoolSize& poolSize = descriptorPoolSizes[index];
                poolSize.descriptorCount += bindingInfo.count;
            }
        }

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        descriptorPoolCreateInfo.poolSizeCount = descriptorPoolSizes.size();
        descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
        descriptorPoolCreateInfo.maxSets = 1;

        if (vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor pool\n");
        }
    }

    {
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set\n");
        }
    }

    // Update descriptor set
    {
        std::vector<VkWriteDescriptorSet> descriptorSetWrites {};
        CapList<VkDescriptorBufferInfo> descBufferInfos { 1024 };
        CapList<VkDescriptorImageInfo> descImageInfos { 1024 };
        std::optional<VkWriteDescriptorSetAccelerationStructureNV> accelStructWrite {};

        for (auto& bindingInfo : shaderBindings()) {

            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.pTexelBufferView = nullptr;

            write.dstSet = descriptorSet;
            write.dstBinding = bindingInfo.bindingIndex;

            switch (bindingInfo.type) {
            case ShaderBindingType::UniformBuffer: {

                ASSERT(bindingInfo.buffers.size() == 1);
                ASSERT(bindingInfo.buffers[0]);
                auto& buffer = dynamic_cast<const VulkanBuffer&>(*bindingInfo.buffers[0]);

                VkDescriptorBufferInfo descBufferInfo {};
                descBufferInfo.offset = 0;
                descBufferInfo.range = VK_WHOLE_SIZE;
                descBufferInfo.buffer = buffer.buffer;

                descBufferInfos.push_back(descBufferInfo);
                write.pBufferInfo = &descBufferInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::StorageBuffer: {

                ASSERT(bindingInfo.buffers.size() == 1);
                ASSERT(bindingInfo.buffers[0]);
                auto& buffer = dynamic_cast<const VulkanBuffer&>(*bindingInfo.buffers[0]);

                VkDescriptorBufferInfo descBufferInfo {};
                descBufferInfo.offset = 0;
                descBufferInfo.range = VK_WHOLE_SIZE;
                descBufferInfo.buffer = buffer.buffer;

                descBufferInfos.push_back(descBufferInfo);
                write.pBufferInfo = &descBufferInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::StorageBufferArray: {

                ASSERT(bindingInfo.count == bindingInfo.buffers.size());

                if (bindingInfo.count == 0) {
                    continue;
                }

                for (const Buffer* buffer : bindingInfo.buffers) {

                    ASSERT(buffer);
                    ASSERT(buffer->usage() == Buffer::Usage::StorageBuffer);
                    auto& vulkanBuffer = dynamic_cast<const VulkanBuffer&>(*bindingInfo.buffers[0]);

                    VkDescriptorBufferInfo descBufferInfo {};
                    descBufferInfo.offset = 0;
                    descBufferInfo.range = VK_WHOLE_SIZE;
                    descBufferInfo.buffer = vulkanBuffer.buffer;

                    descBufferInfos.push_back(descBufferInfo);
                }

                // NOTE: This should point at the first VkDescriptorBufferInfo
                write.pBufferInfo = &descBufferInfos.back() - (bindingInfo.count - 1);
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write.descriptorCount = bindingInfo.count;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::StorageImage: {

                ASSERT(bindingInfo.textures.size() == 1);
                ASSERT(bindingInfo.textures[0]);
                auto& texture = dynamic_cast<const VulkanTexture&>(*bindingInfo.textures[0]);

                VkDescriptorImageInfo descImageInfo {};
                descImageInfo.sampler = texture.sampler;
                descImageInfo.imageView = texture.imageView;

                // The runtime systems make sure that the input texture is in the layout!
                descImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                descImageInfos.push_back(descImageInfo);
                write.pImageInfo = &descImageInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::TextureSampler: {

                ASSERT(bindingInfo.textures.size() == 1);
                ASSERT(bindingInfo.textures[0]);
                auto& texture = dynamic_cast<const VulkanTexture&>(*bindingInfo.textures[0]);

                VkDescriptorImageInfo descImageInfo {};
                descImageInfo.sampler = texture.sampler;
                descImageInfo.imageView = texture.imageView;

                // The runtime systems make sure that the input texture is in the layout!
                descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                descImageInfos.push_back(descImageInfo);
                write.pImageInfo = &descImageInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::TextureSamplerArray: {

                size_t numTextures = bindingInfo.textures.size();
                ASSERT(numTextures > 0);

                for (uint32_t i = 0; i < bindingInfo.count; ++i) {

                    // NOTE: We always have to fill in the count here, but for the unused we just fill with a "default"
                    const Texture* genTexture = (i >= numTextures) ? bindingInfo.textures.front() : bindingInfo.textures[i];
                    ASSERT(genTexture);

                    auto& texture = dynamic_cast<const VulkanTexture&>(*genTexture);

                    VkDescriptorImageInfo descImageInfo {};
                    descImageInfo.sampler = texture.sampler;
                    descImageInfo.imageView = texture.imageView;

                    // The runtime systems make sure that the input texture is in the layout!
                    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    descImageInfos.push_back(descImageInfo);
                }

                // NOTE: This should point at the first VkDescriptorImageInfo
                write.pImageInfo = &descImageInfos.back() - (bindingInfo.count - 1);
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = bindingInfo.count;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::RTAccelerationStructure: {

                ASSERT(bindingInfo.textures.empty());
                ASSERT(bindingInfo.buffers.empty());
                ASSERT(bindingInfo.tlas != nullptr);

                auto& vulkanTlas = dynamic_cast<const VulkanTopLevelAS&>(*bindingInfo.tlas);

                VkWriteDescriptorSetAccelerationStructureNV descriptorAccelerationStructureInfo { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV };
                descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
                descriptorAccelerationStructureInfo.pAccelerationStructures = &vulkanTlas.accelerationStructure;

                // (there can only be one in a set!) (well maybe not, but it makes sense..)
                ASSERT(!accelStructWrite.has_value());
                accelStructWrite = descriptorAccelerationStructureInfo;

                write.pNext = &accelStructWrite.value();
                write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            default:
                ASSERT_NOT_REACHED();
            }

            descriptorSetWrites.push_back(write);
        }

        vkUpdateDescriptorSets(device, descriptorSetWrites.size(), descriptorSetWrites.data(), 0, nullptr);
    }
}

VulkanBindingSet::~VulkanBindingSet()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());
    vkDestroyDescriptorPool(vulkanBackend.device(), descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(vulkanBackend.device(), descriptorSetLayout, nullptr);
}

VulkanRenderState::VulkanRenderState(Backend& backend, const RenderTarget& renderTarget, VertexLayout vertexLayout,
                                     Shader shader, const std::vector<const BindingSet*>& bindingSets,
                                     Viewport viewport, BlendState blendState, RasterState rasterState, DepthState depthState)
    : RenderState(backend, renderTarget, vertexLayout, shader, bindingSets, viewport, blendState, rasterState, depthState)
{
    const auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend);
    const auto& device = vulkanBackend.device();

    VkVertexInputBindingDescription bindingDescription = {};
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions {};
    {
        // TODO: What about multiple bindings? Just have multiple VertexLayout:s?
        uint32_t binding = 0;

        bindingDescription.binding = binding;
        bindingDescription.stride = vertexLayout.vertexStride;
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        attributeDescriptions.reserve(vertexLayout.attributes.size());
        for (const VertexAttribute& attribute : vertexLayout.attributes) {

            VkVertexInputAttributeDescription description = {};
            description.binding = binding;
            description.location = attribute.location;
            description.offset = attribute.memoryOffset;

            VkFormat format;
            switch (attribute.type) {
            case VertexAttributeType::Float2:
                format = VK_FORMAT_R32G32_SFLOAT;
                break;
            case VertexAttributeType::Float3:
                format = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            case VertexAttributeType::Float4:
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            }
            description.format = format;

            attributeDescriptions.push_back(description);
        }
    }

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages {};
    {
        for (auto& file : shader.files()) {

            // TODO: Maybe don't create new modules every time? Currently they are deleted later in this function
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(file.path());
            moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
            moduleCreateInfo.pCode = spirv.data();

            VkShaderModule shaderModule {};
            if (vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                LogErrorAndExit("Error trying to create shader module\n");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.module = shaderModule;
            stageCreateInfo.pName = "main";

            VkShaderStageFlagBits stageFlags;
            switch (file.type()) {
            case ShaderFileType::Vertex:
                stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case ShaderFileType::Fragment:
                stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case ShaderFileType::Compute:
                stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                break;
            default:
                ASSERT_NOT_REACHED();
            }
            stageCreateInfo.stage = stageFlags;

            shaderStages.push_back(stageCreateInfo);
        }
    }

    //
    // Create pipeline layout
    //
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    const auto& [descriptorSetLayouts, pushConstantRange] = vulkanBackend.createDescriptorSetLayoutForShader(shader);

    pipelineLayoutCreateInfo.setLayoutCount = descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    if (pushConstantRange.has_value()) {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange.value();
    } else {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
    }

    if (vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create pipeline layout\n");
    }

    // (it's *probably* safe to delete these after creating the pipeline layout! no layers are complaining)
    for (const VkDescriptorSetLayout& layout : descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
    }

    //
    // Create pipeline
    //
    VkPipelineVertexInputStateCreateInfo vertInputState = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertInputState.vertexBindingDescriptionCount = 1;
    vertInputState.pVertexBindingDescriptions = &bindingDescription;
    vertInputState.vertexAttributeDescriptionCount = attributeDescriptions.size();
    vertInputState.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyState.primitiveRestartEnable = VK_FALSE;

    VkViewport vkViewport = {};
    vkViewport.x = fixedViewport().x;
    vkViewport.y = fixedViewport().y;
    vkViewport.width = fixedViewport().extent.width();
    vkViewport.height = fixedViewport().extent.height();
    vkViewport.minDepth = 0.0f;
    vkViewport.maxDepth = 1.0f;

    // TODO: Should we always use the viewport settings if no scissor is specified?
    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent.width = uint32_t(fixedViewport().extent.width());
    scissor.extent.height = uint32_t(fixedViewport().extent.height());

    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.pViewports = &vkViewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    switch (rasterState.polygonMode) {
    case PolygonMode::Filled:
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        break;
    case PolygonMode::Lines:
        rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        break;
    case PolygonMode::Points:
        rasterizer.polygonMode = VK_POLYGON_MODE_POINT;
        break;
    }

    if (rasterState.backfaceCullingEnabled) {
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    } else {
        rasterizer.cullMode = VK_CULL_MODE_NONE;
    }

    switch (rasterState.frontFace) {
    case TriangleWindingOrder::Clockwise:
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        break;
    case TriangleWindingOrder::CounterClockwise:
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        break;
    }

    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments {};
    if (blendState.enabled) {
        // TODO: Implement blending!
        ASSERT_NOT_REACHED();
    } else {
        renderTarget.forEachColorAttachment([&](const RenderTarget::Attachment& attachment) {
            VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // NOLINT(hicpp-signed-bitwise)
            colorBlendAttachment.blendEnable = VK_FALSE;
            colorBlendAttachments.push_back(colorBlendAttachment);
        });
    }
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = colorBlendAttachments.size();
    colorBlending.pAttachments = colorBlendAttachments.data();

    VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencilState.depthTestEnable = depthState.testDepth;
    depthStencilState.depthWriteEnable = depthState.writeDepth;
    depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencilState.depthBoundsTestEnable = VK_FALSE;
    depthStencilState.minDepthBounds = 0.0f;
    depthStencilState.maxDepthBounds = 1.0f;
    depthStencilState.stencilTestEnable = VK_FALSE;
    depthStencilState.front = {};
    depthStencilState.back = {};

    VkGraphicsPipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

    // stages
    pipelineCreateInfo.stageCount = shaderStages.size();
    pipelineCreateInfo.pStages = shaderStages.data();

    // fixed function stuff
    pipelineCreateInfo.pVertexInputState = &vertInputState;
    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampling;
    pipelineCreateInfo.pDepthStencilState = &depthStencilState;
    pipelineCreateInfo.pColorBlendState = &colorBlending;
    pipelineCreateInfo.pDynamicState = nullptr;

    // pipeline layout
    pipelineCreateInfo.layout = pipelineLayout;

    // render pass stuff
    auto& vulkanRenderTarget = dynamic_cast<const VulkanRenderTarget&>(renderTarget);
    pipelineCreateInfo.renderPass = vulkanRenderTarget.compatibleRenderPass;
    pipelineCreateInfo.subpass = 0; // TODO: How should this be handled?

    // extra stuff (optional for this)
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineCreateInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create graphics pipeline\n");
    }

    // Remove shader modules, they are no longer needed after creating the pipeline
    for (auto& stage : shaderStages) {
        vkDestroyShaderModule(device, stage.module, nullptr);
    }

    for (auto& set : bindingSets) {
        for (auto& bindingInfo : set->shaderBindings()) {
            for (auto texture : bindingInfo.textures) {
                sampledTextures.push_back(texture);
            }
        }
    }
}

VulkanRenderState::~VulkanRenderState()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());
    vkDestroyPipeline(vulkanBackend.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(vulkanBackend.device(), pipelineLayout, nullptr);
}

VulkanTopLevelAS::VulkanTopLevelAS(Backend& backend, std::vector<RTGeometryInstance> inst)
    : TopLevelAS(backend, std::move(inst))
{
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend);
    MOOSLIB_ASSERT(vulkanBackend.hasRtxSupport());

    // Something more here maybe? Like fast to build/traverse, can be compacted, etc.
    auto flags = VkBuildAccelerationStructureFlagBitsNV(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV);

    VkAccelerationStructureInfoNV accelerationStructureInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    accelerationStructureInfo.flags = flags;
    accelerationStructureInfo.instanceCount = instanceCount();
    accelerationStructureInfo.geometryCount = 0;

    VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
    accelerationStructureCreateInfo.info = accelerationStructureInfo;
    if (vulkanBackend.rtx().vkCreateAccelerationStructureNV(vulkanBackend.device(), &accelerationStructureCreateInfo, nullptr, &accelerationStructure) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create top level acceleration structure\n");
    }

    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
    memoryRequirementsInfo.accelerationStructure = accelerationStructure;
    VkMemoryRequirements2 memoryRequirements2 {};
    vulkanBackend.rtx().vkGetAccelerationStructureMemoryRequirementsNV(vulkanBackend.device(), &memoryRequirementsInfo, &memoryRequirements2);

    VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memoryAllocateInfo.allocationSize = memoryRequirements2.memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = vulkanBackend.findAppropriateMemory(memoryRequirements2.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vulkanBackend.device(), &memoryAllocateInfo, nullptr, &memory) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create allocate memory for acceleration structure\n");
    }

    VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo { VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV };
    accelerationStructureMemoryInfo.accelerationStructure = accelerationStructure;
    accelerationStructureMemoryInfo.memory = memory;
    if (vulkanBackend.rtx().vkBindAccelerationStructureMemoryNV(vulkanBackend.device(), 1, &accelerationStructureMemoryInfo) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to bind memory to acceleration structure\n");
    }

    if (vulkanBackend.rtx().vkGetAccelerationStructureHandleNV(vulkanBackend.device(), accelerationStructure, sizeof(uint64_t), &handle) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to get acceleration structure handle\n");
    }

    VmaAllocation scratchAllocation;
    VkBuffer scratchBuffer = vulkanBackend.rtx().createScratchBufferForAccelerationStructure(accelerationStructure, false, scratchAllocation);

    VmaAllocation instanceAllocation;
    VkBuffer instanceBuffer = vulkanBackend.rtx().createInstanceBuffer(instances(), instanceAllocation);

    VkAccelerationStructureInfoNV buildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    buildInfo.flags = flags;
    buildInfo.instanceCount = instanceCount();
    buildInfo.geometryCount = 0;
    buildInfo.pGeometries = nullptr;

    vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vulkanBackend.rtx().vkCmdBuildAccelerationStructureNV(
            commandBuffer,
            &buildInfo,
            instanceBuffer, 0,
            VK_FALSE,
            accelerationStructure,
            VK_NULL_HANDLE,
            scratchBuffer, 0);
    });

    vmaDestroyBuffer(vulkanBackend.globalAllocator(), scratchBuffer, scratchAllocation);

    // (should persist for the lifetime of this TLAS)
    associatedBuffers.push_back({ instanceBuffer, instanceAllocation });
}

VulkanTopLevelAS::~VulkanTopLevelAS()
{
    if (!hasBackend())
        return;

    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());
    vulkanBackend.rtx().vkDestroyAccelerationStructureNV(vulkanBackend.device(), accelerationStructure, nullptr);
    vkFreeMemory(vulkanBackend.device(), memory, nullptr);

    for (auto& [buffer, allocation] : associatedBuffers) {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), buffer, allocation);
    }
}

VulkanBottomLevelAS::VulkanBottomLevelAS(Backend& backend, std::vector<RTGeometry> geos)
    : BottomLevelAS(backend, std::move(geos))
{
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend);
    MOOSLIB_ASSERT(vulkanBackend.hasRtxSupport());

    // All geometries in a BLAS must have the same type (i.e. AABB/triangles)
    bool isTriangleBLAS = geometries().front().hasTriangles();
    for (size_t i = 1; i < geometries().size(); ++i) {
        ASSERT(geometries()[i].hasTriangles() == isTriangleBLAS);
    }

    VkBuffer transformBuffer;
    VmaAllocation transformBufferAllocation;
    size_t singleTransformSize = 3 * 4 * sizeof(float);
    if (isTriangleBLAS) {
        std::vector<moos::mat3x4> transforms {};
        for (auto& geo : geometries()) {
            moos::mat3x4 mat34 = transpose(geo.triangles().transform);
            transforms.push_back(mat34);
        }

        size_t totalSize = transforms.size() * singleTransformSize;

        VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV; // (I can't find info on usage from the spec, but I assume this should work)
        bufferCreateInfo.size = totalSize;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        if (vmaCreateBuffer(vulkanBackend.globalAllocator(), &bufferCreateInfo, &allocCreateInfo, &transformBuffer, &transformBufferAllocation, nullptr) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create buffer for the bottom level acceeration structure transforms.\n");
        }

        if (!vulkanBackend.setBufferMemoryUsingMapping(transformBufferAllocation, transforms.data(), totalSize)) {
            LogErrorAndExit("Error trying to copy data to the bottom level acceeration structure transform buffer.\n");
        }
    }

    std::vector<VkGeometryNV> vkGeometries {};

    for (size_t geoIdx = 0; geoIdx < geometries().size(); ++geoIdx) {
        const RTGeometry& geo = geometries()[geoIdx];

        if (geo.hasTriangles()) {
            const RTTriangleGeometry& triGeo = geo.triangles();

            VkGeometryTrianglesNV triangles { VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV };

            triangles.vertexData = dynamic_cast<const VulkanBuffer&>(triGeo.vertexBuffer).buffer;
            triangles.vertexOffset = 0;
            triangles.vertexStride = triGeo.vertexStride;
            triangles.vertexCount = triGeo.vertexBuffer.size() / triangles.vertexStride;
            switch (triGeo.vertexFormat) {
            case VertexFormat::XYZ32F:
                triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            }

            triangles.indexData = dynamic_cast<const VulkanBuffer&>(triGeo.indexBuffer).buffer;
            triangles.indexOffset = 0;
            switch (triGeo.indexType) {
            case IndexType::UInt16:
                triangles.indexType = VK_INDEX_TYPE_UINT16;
                triangles.indexCount = triGeo.indexBuffer.size() / sizeof(uint16_t);
                break;
            case IndexType::UInt32:
                triangles.indexType = VK_INDEX_TYPE_UINT32;
                triangles.indexCount = triGeo.indexBuffer.size() / sizeof(uint32_t);
                break;
            }

            triangles.transformData = transformBuffer;
            triangles.transformOffset = geoIdx * singleTransformSize;

            VkGeometryNV geometry { VK_STRUCTURE_TYPE_GEOMETRY_NV };
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV; // "indicates that this geometry does not invoke the any-hit shaders even if present in a hit group."

            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
            geometry.geometry.triangles = triangles;

            VkGeometryAABBNV aabbs { VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV };
            aabbs.numAABBs = 0;
            geometry.geometry.aabbs = aabbs;

            vkGeometries.push_back(geometry);
        }

        else if (geo.hasAABBs()) {
            const RTAABBGeometry& aabbGeo = geo.aabbs();

            VkGeometryAABBNV aabbs { VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV };
            aabbs.offset = 0;
            aabbs.stride = aabbGeo.aabbStride;
            aabbs.aabbData = dynamic_cast<const VulkanBuffer&>(aabbGeo.aabbBuffer).buffer;
            aabbs.numAABBs = aabbGeo.aabbBuffer.size() / aabbGeo.aabbStride;

            VkGeometryNV geometry { VK_STRUCTURE_TYPE_GEOMETRY_NV };
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV; // "indicates that this geometry does not invoke the any-hit shaders even if present in a hit group."

            geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_NV;
            geometry.geometry.aabbs = aabbs;

            VkGeometryTrianglesNV triangles { VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV };
            triangles.vertexCount = 0;
            triangles.indexCount = 0;
            geometry.geometry.triangles = triangles;

            vkGeometries.push_back(geometry);
        }
    }

    VkAccelerationStructureInfoNV accelerationStructureInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
    accelerationStructureInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
    accelerationStructureInfo.instanceCount = 0;
    accelerationStructureInfo.geometryCount = vkGeometries.size();
    accelerationStructureInfo.pGeometries = vkGeometries.data();

    VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
    accelerationStructureCreateInfo.info = accelerationStructureInfo;
    if (vulkanBackend.rtx().vkCreateAccelerationStructureNV(vulkanBackend.device(), &accelerationStructureCreateInfo, nullptr, &accelerationStructure) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create bottom level acceleration structure\n");
    }

    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
    memoryRequirementsInfo.accelerationStructure = accelerationStructure;
    VkMemoryRequirements2 memoryRequirements2 {};
    vulkanBackend.rtx().vkGetAccelerationStructureMemoryRequirementsNV(vulkanBackend.device(), &memoryRequirementsInfo, &memoryRequirements2);

    VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memoryAllocateInfo.allocationSize = memoryRequirements2.memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = vulkanBackend.findAppropriateMemory(memoryRequirements2.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vulkanBackend.device(), &memoryAllocateInfo, nullptr, &memory) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create allocate memory for acceleration structure\n");
    }

    VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo { VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV };
    accelerationStructureMemoryInfo.accelerationStructure = accelerationStructure;
    accelerationStructureMemoryInfo.memory = memory;
    if (vulkanBackend.rtx().vkBindAccelerationStructureMemoryNV(vulkanBackend.device(), 1, &accelerationStructureMemoryInfo) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to bind memory to acceleration structure\n");
    }

    if (vulkanBackend.rtx().vkGetAccelerationStructureHandleNV(vulkanBackend.device(), accelerationStructure, sizeof(uint64_t), &handle) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to get acceleration structure handle\n");
    }

    VmaAllocation scratchAllocation;
    VkBuffer scratchBuffer = vulkanBackend.rtx().createScratchBufferForAccelerationStructure(accelerationStructure, false, scratchAllocation);

    VkAccelerationStructureInfoNV buildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
    buildInfo.geometryCount = vkGeometries.size();
    buildInfo.pGeometries = vkGeometries.data();

    vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vulkanBackend.rtx().vkCmdBuildAccelerationStructureNV(
            commandBuffer,
            &buildInfo,
            VK_NULL_HANDLE, 0,
            VK_FALSE,
            accelerationStructure,
            VK_NULL_HANDLE,
            scratchBuffer, 0);
    });

    vmaDestroyBuffer(vulkanBackend.globalAllocator(), scratchBuffer, scratchAllocation);

    if (isTriangleBLAS) {
        // (should persist for the lifetime of this BLAS)
        associatedBuffers.push_back({ transformBuffer, transformBufferAllocation });
    }
}

VulkanBottomLevelAS::~VulkanBottomLevelAS()
{
    if (!hasBackend())
        return;

    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());
    vulkanBackend.rtx().vkDestroyAccelerationStructureNV(vulkanBackend.device(), accelerationStructure, nullptr);
    vkFreeMemory(vulkanBackend.device(), memory, nullptr);

    for (auto& [buffer, allocation] : associatedBuffers) {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), buffer, allocation);
    }
}

VulkanRayTracingState::VulkanRayTracingState(Backend& backend, ShaderBindingTable sbt, std::vector<const BindingSet*> bindingSets, uint32_t maxRecursionDepth)
    : RayTracingState(backend, sbt, bindingSets, maxRecursionDepth)
{
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend);
    MOOSLIB_ASSERT(vulkanBackend.hasRtxSupport());

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts {};
    for (auto& set : bindingSets) {
        auto& vulkanBindingSet = dynamic_cast<const VulkanBindingSet&>(*set);
        descriptorSetLayouts.push_back(vulkanBindingSet.descriptorSetLayout);
    }

    // TODO: Really, it makes sense to use the descriptor set layouts we get from the helper function as well.
    //  However, the problem is that we have dynamic-length storage buffers in our ray tracing shaders, and if
    //  I'm not mistaken we need to specify the length of them in the layout. The passed in stuff should include
    //  the actual array so we know the length in that case. Without the input data though we don't know that.
    //  We will have to think about what the best way to handle that would be..
    Shader shader { shaderBindingTable().allReferencedShaderFiles(), ShaderType::RayTrace };
    const auto& [tempDescriptorSetLayouts, pushConstantRange] = vulkanBackend.createDescriptorSetLayoutForShader(shader);
    for (auto& dsl : tempDescriptorSetLayouts) { // FIXME: This is obviously very stupid..
        vkDestroyDescriptorSetLayout(vulkanBackend.device(), dsl, nullptr);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    pipelineLayoutCreateInfo.setLayoutCount = descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    if (pushConstantRange.has_value()) {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange.value();
    } else {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
    }

    if (vkCreatePipelineLayout(vulkanBackend.device(), &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create pipeline layout for ray tracing\n");
    }

    std::vector<VkShaderModule> shaderModulesToRemove {};
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages {};
    std::vector<VkRayTracingShaderGroupCreateInfoNV> shaderGroups {};

    // RayGen
    {
        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(sbt.rayGen().path());
        moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
        moduleCreateInfo.pCode = spirv.data();

        VkShaderModule shaderModule {};
        if (vkCreateShaderModule(vulkanBackend.device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create shader module for raygen shader for ray tracing state\n");
        }

        VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageCreateInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_NV;
        stageCreateInfo.module = shaderModule;
        stageCreateInfo.pName = "main";

        uint32_t shaderIndex = shaderStages.size();
        shaderStages.push_back(stageCreateInfo);
        shaderModulesToRemove.push_back(shaderModule);

        VkRayTracingShaderGroupCreateInfoNV shaderGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
        shaderGroup.generalShader = shaderIndex;

        shaderGroup.closestHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_NV;

        shaderGroups.push_back(shaderGroup);
    }

    // HitGroups
    for (const HitGroup& hitGroup : sbt.hitGroups()) {

        VkRayTracingShaderGroupCreateInfoNV shaderGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };

        shaderGroup.type = hitGroup.hasIntersectionShader()
            ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_NV
            : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV;

        shaderGroup.generalShader = VK_SHADER_UNUSED_NV;
        shaderGroup.closestHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_NV;

        // ClosestHit
        {
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(hitGroup.closestHit().path());
            moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
            moduleCreateInfo.pCode = spirv.data();

            VkShaderModule shaderModule {};
            if (vkCreateShaderModule(vulkanBackend.device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                LogErrorAndExit("Error trying to create shader module for closest hit shader for ray tracing state\n");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
            stageCreateInfo.module = shaderModule;
            stageCreateInfo.pName = "main";

            shaderGroup.closestHitShader = shaderStages.size();
            shaderStages.push_back(stageCreateInfo);
            shaderModulesToRemove.push_back(shaderModule);
        }

        ASSERT(!hitGroup.hasAnyHitShader()); // for now!

        if (hitGroup.hasIntersectionShader()) {
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(hitGroup.intersection().path());
            moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
            moduleCreateInfo.pCode = spirv.data();

            VkShaderModule shaderModule {};
            if (vkCreateShaderModule(vulkanBackend.device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                LogErrorAndExit("Error trying to create shader module for intersection shader for ray tracing state\n");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.stage = VK_SHADER_STAGE_INTERSECTION_BIT_NV;
            stageCreateInfo.module = shaderModule;
            stageCreateInfo.pName = "main";

            shaderGroup.intersectionShader = shaderStages.size();
            shaderStages.push_back(stageCreateInfo);
            shaderModulesToRemove.push_back(shaderModule);
        }

        shaderGroups.push_back(shaderGroup);
    }

    // Miss shaders
    for (const ShaderFile& missShader : sbt.missShaders()) {

        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(missShader.path());
        moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
        moduleCreateInfo.pCode = spirv.data();

        VkShaderModule shaderModule {};
        if (vkCreateShaderModule(vulkanBackend.device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create shader module for miss shader for ray tracing state\n");
        }

        VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageCreateInfo.stage = VK_SHADER_STAGE_MISS_BIT_NV;
        stageCreateInfo.module = shaderModule;
        stageCreateInfo.pName = "main";

        uint32_t shaderIndex = shaderStages.size();
        shaderStages.push_back(stageCreateInfo);
        shaderModulesToRemove.push_back(shaderModule);

        VkRayTracingShaderGroupCreateInfoNV shaderGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV };
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
        shaderGroup.generalShader = shaderIndex;

        shaderGroup.closestHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_NV;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_NV;

        shaderGroups.push_back(shaderGroup);
    }

    VkRayTracingPipelineCreateInfoNV rtPipelineCreateInfo { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV };
    rtPipelineCreateInfo.maxRecursionDepth = maxRecursionDepth;
    rtPipelineCreateInfo.stageCount = shaderStages.size();
    rtPipelineCreateInfo.pStages = shaderStages.data();
    rtPipelineCreateInfo.groupCount = shaderGroups.size();
    rtPipelineCreateInfo.pGroups = shaderGroups.data();
    rtPipelineCreateInfo.layout = pipelineLayout;

    if (vulkanBackend.rtx().vkCreateRayTracingPipelinesNV(vulkanBackend.device(), VK_NULL_HANDLE, 1, &rtPipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        LogErrorAndExit("Error creating ray tracing pipeline\n");
    }

    // Remove shader modules after creating the pipeline
    for (VkShaderModule& shaderModule : shaderModulesToRemove) {
        vkDestroyShaderModule(vulkanBackend.device(), shaderModule, nullptr);
    }

    // Create buffer for the shader binding table
    {
        uint32_t sizeOfSingleHandle = vulkanBackend.rtx().properties().shaderGroupHandleSize;
        uint32_t sizeOfAllHandles = sizeOfSingleHandle * shaderGroups.size();
        std::vector<std::byte> shaderGroupHandles { sizeOfAllHandles };
        if (vulkanBackend.rtx().vkGetRayTracingShaderGroupHandlesNV(vulkanBackend.device(), pipeline, 0, shaderGroups.size(), sizeOfAllHandles, shaderGroupHandles.data()) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to get shader group handles for the shader binding table.\n");
        }

        // TODO: For now we don't have any data, only shader handles, but we still have to consider the alignments & strides
        uint32_t baseAlignment = vulkanBackend.rtx().properties().shaderGroupBaseAlignment;
        uint32_t sbtSize = baseAlignment * shaderGroups.size();
        std::vector<std::byte> sbtData { sbtSize };

        for (size_t i = 0; i < shaderGroups.size(); ++i) {

            uint32_t srcOffset = i * sizeOfSingleHandle;
            uint32_t dstOffset = i * baseAlignment;

            std::copy(shaderGroupHandles.begin() + srcOffset,
                      shaderGroupHandles.begin() + srcOffset + sizeOfSingleHandle,
                      sbtData.begin() + dstOffset);
        }

        VkBufferCreateInfo sbtBufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        sbtBufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
        sbtBufferCreateInfo.size = sbtSize;

        if (vulkanDebugMode) {
            // for nsight debugging & similar stuff)
            sbtBufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }

        VmaAllocationCreateInfo sbtAllocCreateInfo = {};
        sbtAllocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU; // Gpu only is probably perfectly fine, except we need to copy the data using a staging buffer

        if (vmaCreateBuffer(vulkanBackend.globalAllocator(), &sbtBufferCreateInfo, &sbtAllocCreateInfo, &sbtBuffer, &sbtBufferAllocation, nullptr) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create buffer for the shader binding table.\n");
        }

        if (!vulkanBackend.setBufferMemoryUsingMapping(sbtBufferAllocation, sbtData.data(), sbtSize)) {
            LogErrorAndExit("Error trying to copy data to the shader binding table.\n");
        }
    }

    for (auto& set : bindingSets) {
        for (auto& bindingInfo : set->shaderBindings()) {
            for (auto texture : bindingInfo.textures) {
                switch (bindingInfo.type) {
                case ShaderBindingType::TextureSampler:
                case ShaderBindingType::TextureSamplerArray:
                    sampledTextures.push_back(texture);
                    break;
                case ShaderBindingType::StorageImage:
                    storageImages.push_back(texture);
                    break;
                default:
                    ASSERT_NOT_REACHED();
                }
            }
        }
    }
}

VulkanRayTracingState::~VulkanRayTracingState()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), sbtBuffer, sbtBufferAllocation);
    vkDestroyPipeline(vulkanBackend.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(vulkanBackend.device(), pipelineLayout, nullptr);
}

VulkanComputeState::VulkanComputeState(Backend& backend, Shader shader, std::vector<const BindingSet*> bindingSets)
    : ComputeState(backend, shader, bindingSets)
{
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend);

    VkPipelineShaderStageCreateInfo computeShaderStage { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    computeShaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStage.pName = "main";
    {
        ASSERT(shader.type() == ShaderType::Compute);
        ASSERT(shader.files().size() == 1);

        const ShaderFile& file = shader.files().front();
        ASSERT(file.type() == ShaderFileType::Compute);

        // TODO: Maybe don't create new modules every time? Currently they are deleted later in this function
        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(file.path());
        moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
        moduleCreateInfo.pCode = spirv.data();

        VkShaderModule shaderModule {};
        if (vkCreateShaderModule(vulkanBackend.device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create shader module\n");
        }

        computeShaderStage.module = shaderModule;
    }

    //
    // Create pipeline layout
    //

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    const auto& [descriptorSetLayouts, pushConstantRange] = vulkanBackend.createDescriptorSetLayoutForShader(shader);

    pipelineLayoutCreateInfo.setLayoutCount = descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    if (pushConstantRange.has_value()) {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange.value();
    } else {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
    }

    if (vkCreatePipelineLayout(vulkanBackend.device(), &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create pipeline layout\n");
    }

    // (it's *probably* safe to delete these after creating the pipeline layout! no layers are complaining)
    for (const VkDescriptorSetLayout& layout : descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(vulkanBackend.device(), layout, nullptr);
    }

    //
    // Create pipeline
    //

    VkComputePipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };

    pipelineCreateInfo.stage = computeShaderStage;
    pipelineCreateInfo.layout = pipelineLayout;
    pipelineCreateInfo.flags = 0u;

    if (vkCreateComputePipelines(vulkanBackend.device(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create compute pipeline\n");
    }

    // Remove shader modules, they are no longer needed after creating the pipeline
    vkDestroyShaderModule(vulkanBackend.device(), computeShaderStage.module, nullptr);

    for (auto& set : bindingSets) {
        for (auto& bindingInfo : set->shaderBindings()) {
            for (auto texture : bindingInfo.textures) {
                switch (bindingInfo.type) {
                case ShaderBindingType::StorageImage:
                    storageImages.push_back(texture);
                    break;
                default:
                    ASSERT_NOT_REACHED();
                }
            }
        }
    }
}

VulkanComputeState::~VulkanComputeState()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = dynamic_cast<VulkanBackend&>(backend());
    vkDestroyPipeline(vulkanBackend.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(vulkanBackend.device(), pipelineLayout, nullptr);
}
