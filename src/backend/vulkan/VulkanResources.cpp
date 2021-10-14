#include "VulkanResources.h"

#include "backend/vulkan/VulkanBackend.h"
#include "rendering/ShaderManager.h"
#include "utility/CapList.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <moos/core.h>
#include <stb_image.h>

VulkanBuffer::VulkanBuffer(Backend& backend, size_t size, Usage usage, MemoryHint memoryHint)
    : Buffer(backend, size, usage, memoryHint)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();
    createInternal(size, buffer, allocation);
}

VulkanBuffer::~VulkanBuffer()
{
    destroyInternal(buffer, allocation);
}

void VulkanBuffer::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(buffer);
        nameInfo.pObjectName = name.c_str();

        if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
            LogWarning("Could not set debug name for vulkan buffer resource.\n");
        }
    }
}

void VulkanBuffer::updateData(const std::byte* data, size_t updateSize, size_t offset)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (updateSize == 0)
        return;
    if (offset + updateSize > size())
        LogErrorAndExit("Attempt at updating buffer outside of bounds!\n");

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    switch (memoryHint()) {
    case Buffer::MemoryHint::GpuOptimal:
        if (!vulkanBackend.setBufferDataUsingStagingBuffer(buffer, (uint8_t*)data, updateSize, offset)) {
            LogError("Could not update the data of GPU-optimal buffer\n");
        }
        break;
    case Buffer::MemoryHint::TransferOptimal:
        if (!vulkanBackend.setBufferMemoryUsingMapping(allocation, (uint8_t*)data, updateSize, offset)) {
            LogError("Could not update the data of transfer-optimal buffer\n");
        }
        break;
    case Buffer::MemoryHint::GpuOnly:
        LogError("Can't update buffer with GpuOnly memory hint, ignoring\n");
        break;
    case Buffer::MemoryHint::Readback:
        LogError("Can't update buffer with Readback memory hint, ignoring\n");
        break;
    }
}

void VulkanBuffer::reallocateWithSize(size_t newSize, ReallocateStrategy strategy)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (strategy == ReallocateStrategy::CopyExistingData && newSize < size())
        LogErrorAndExit("Can't reallocate buffer ReallocateStrategy::CopyExistingData if the new size is smaller than the current size!");

    switch (strategy) {
    case ReallocateStrategy::DiscardExistingData:

        destroyInternal(buffer, allocation);
        createInternal(newSize, buffer, allocation);
        this->m_size = newSize;

        break;

    case ReallocateStrategy::CopyExistingData:

        VkBuffer newBuffer;
        VmaAllocation newAllocation;
        createInternal(newSize, newBuffer, newAllocation);

        auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
        vulkanBackend.copyBuffer(buffer, newBuffer, size());

        destroyInternal(buffer, allocation);

        buffer = newBuffer;
        allocation = newAllocation;
        this->m_size = newSize;

        break;
    }

    // Re-set GPU buffer name for the new resource
    if (!name().empty())
        setName(name());
}

void VulkanBuffer::createInternal(size_t size, VkBuffer& outBuffer, VmaAllocation& outAllocation)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

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
    switch (usage()) {
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
    case Buffer::Usage::IndirectBuffer:
        usageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        break;
    case Buffer::Usage::Transfer:
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    // Always make vertex & index buffers also have storage buffer support, so the buffers can be reused for ray tracing shaders
    if (usage() == Buffer::Usage::Vertex || usage() == Buffer::Usage::Index) {
        usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }

    if (vulkanDebugMode) {
        // for nsight debugging & similar stuff)
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo allocCreateInfo = {};
    switch (memoryHint()) {
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

    auto& allocator = static_cast<VulkanBackend&>(backend()).globalAllocator();

    VmaAllocationInfo allocationInfo;
    if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &outBuffer, &outAllocation, &allocationInfo) != VK_SUCCESS) {
        LogErrorAndExit("Could not create buffer of size %u.\n", size);
    }
}

void VulkanBuffer::destroyInternal(VkBuffer inBuffer, VmaAllocation inAllocation)
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), inBuffer, inAllocation);
}

VulkanTexture::VulkanTexture(Backend& backend, TextureDescription desc)
    : Texture(backend, desc)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    // HACK: Now we longer specify what usage we want for the texture, and instead always select all
    //  possible capabilities. However, some texture formats (e.g. sRGB formats) do not support being
    //  used as a storage image, so we need to explicitly disable it for those formats.
    bool storageCapable = true;

    switch (format()) {
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
    case Texture::Format::R32F:
        vkFormat = VK_FORMAT_R32_SFLOAT;
        break;
    case Texture::Format::RG16F:
        vkFormat = VK_FORMAT_R16G16_SFLOAT;
        break;
    case Texture::Format::RG32F:
        vkFormat = VK_FORMAT_R32G32_SFLOAT;
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
    case Texture::Format::Depth24Stencil8:
        vkFormat = VK_FORMAT_D24_UNORM_S8_UINT;
        storageCapable = false;
        break;
    case Texture::Format::Unknown:
        LogErrorAndExit("Trying to create new texture with format Unknown, which is not allowed!\n");
    default:
        ASSERT_NOT_REACHED();
    }

    // Unless we want to enable the multisampled storage images feature we can't have that.. So let's just not, for now..
    // The Vulkan spec states: If the multisampled storage images feature is not enabled, and usage contains VK_IMAGE_USAGE_STORAGE_BIT, samples must be VK_SAMPLE_COUNT_1_BIT
    if (multisampling() != Texture::Multisampling::None) {
        storageCapable = false;
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
    imageCreateInfo.extent = { .width = extent().width(), .height = extent().height(), .depth = 1 };
    imageCreateInfo.mipLevels = mipLevels();
    imageCreateInfo.usage = usageFlags;
    imageCreateInfo.format = vkFormat;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.samples = static_cast<VkSampleCountFlagBits>(multisampling());
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkUsage = usageFlags;

    switch (type()) {
    case Type::Texture2D:
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.arrayLayers = arrayCount();
        break;
    case Type::Cubemap:
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageCreateInfo.arrayLayers = 6 * arrayCount();
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    {
        SCOPED_PROFILE_ZONE_NAMED("vmaCreateImage");
        auto& allocator = static_cast<VulkanBackend&>(backend).globalAllocator();
        if (vmaCreateImage(allocator, &imageCreateInfo, &allocCreateInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
            LogError("VulkanBackend::newTexture(): could not create image.\n");
        }
    }

    VkImageAspectFlags aspectFlags = 0u;
    if (hasDepthFormat()) {
        // Create view for the depth aspect only
        aspectFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        aspectFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkImageViewCreateInfo viewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
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

    switch (type()) {
    case Type::Texture2D:
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = arrayCount();
        viewCreateInfo.viewType = isArray()
            ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
            : VK_IMAGE_VIEW_TYPE_2D;
        break;
    case Type::Cubemap:
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = 6 * arrayCount();
        viewCreateInfo.viewType = isArray()
            ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY
            : VK_IMAGE_VIEW_TYPE_CUBE;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    VkDevice device = static_cast<VulkanBackend&>(backend).device();
    if (vkCreateImageView(device, &viewCreateInfo, nullptr, &imageView) != VK_SUCCESS) {
        LogError("VulkanBackend::newTexture(): could not create image view.\n");
    }

    VkFilter vkMinFilter;
    switch (minFilter()) {
    case Texture::MinFilter::Linear:
        vkMinFilter = VK_FILTER_LINEAR;
        break;
    case Texture::MinFilter::Nearest:
        vkMinFilter = VK_FILTER_NEAREST;
        break;
    default:
        vkMinFilter = VK_FILTER_MAX_ENUM;
        ASSERT_NOT_REACHED();
    }

    VkFilter vkMagFilter;
    switch (magFilter()) {
    case Texture::MagFilter::Linear:
        vkMagFilter = VK_FILTER_LINEAR;
        break;
    case Texture::MagFilter::Nearest:
        vkMagFilter = VK_FILTER_NEAREST;
        break;
    default:
        vkMagFilter = VK_FILTER_MAX_ENUM;
        ASSERT_NOT_REACHED();
    }

    auto wrapModeToAddressMode = [](WrapMode mode) -> VkSamplerAddressMode {
        switch (mode) {
        case WrapMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case WrapMode::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case WrapMode::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        default:
            ASSERT_NOT_REACHED();
        }
    };

    VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    samplerCreateInfo.magFilter = vkMagFilter;
    samplerCreateInfo.minFilter = vkMinFilter;
    samplerCreateInfo.addressModeU = wrapModeToAddressMode(wrapMode().u);
    samplerCreateInfo.addressModeV = wrapModeToAddressMode(wrapMode().v);
    samplerCreateInfo.addressModeW = wrapModeToAddressMode(wrapMode().w);
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
        samplerCreateInfo.maxLod = static_cast<float>(mipLevels());
        break;
    case Texture::Mipmap::Linear:
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.maxLod = static_cast<float>(mipLevels());
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
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vkDestroySampler(vulkanBackend.device(), sampler, nullptr);
    vkDestroyImageView(vulkanBackend.device(), imageView, nullptr);
    vmaDestroyImage(vulkanBackend.globalAllocator(), image, allocation);
}

void VulkanTexture::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        std::string imageViewName = name + "-view";
        std::string samplerName = name + "-sampler";

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(image);
            nameInfo.pObjectName = name.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan image resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(imageView);
            nameInfo.pObjectName = imageViewName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan image view resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_SAMPLER;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(sampler);
            nameInfo.pObjectName = samplerName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan sampler resource.\n");
            }
        }
    }
}

void VulkanTexture::clear(ClearColor color)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    // TODO: Support depth texture clears!
    ASSERT(!hasDepthFormat());

    std::optional<VkImageLayout> originalLayout;
    if (currentLayout != VK_IMAGE_LAYOUT_GENERAL && currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        originalLayout = currentLayout;

        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = originalLayout.value();
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = mipLevels();
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = layerCount();

        // FIXME: Probably overly aggressive barriers!

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        bool success = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer,
                                 sourceStage, destinationStage, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        });
        if (!success) {
            LogError("Could not transition image to general layout.\n");
            return;
        }
    }

    VkClearColorValue clearValue {};
    clearValue.float32[0] = color.r;
    clearValue.float32[1] = color.g;
    clearValue.float32[2] = color.b;
    clearValue.float32[3] = color.a;

    VkImageSubresourceRange range {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    range.baseMipLevel = 0;
    range.levelCount = mipLevels();

    range.baseArrayLayer = 0;
    range.layerCount = layerCount();

    bool success = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vkCmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &range);
    });
    if (!success) {
        LogError("Could not clear the color image.\n");
        return;
    }

    if (originalLayout.has_value() && originalLayout.value() != VK_IMAGE_LAYOUT_UNDEFINED && originalLayout.value() != VK_IMAGE_LAYOUT_PREINITIALIZED) {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.newLayout = originalLayout.value();
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = mipLevels();
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = layerCount();

        // FIXME: Probably overly aggressive barriers!

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        bool success = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer,
                                 sourceStage, destinationStage, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        });
        if (!success) {
            LogError("Could not transition image back to original layout.\n");
            return;
        }
    }
}

void VulkanTexture::setPixelData(vec4 pixel)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    int numChannels;
    bool isHdr = false;

    switch (format()) {
    case Texture::Format::R32:
        numChannels = 1;
        isHdr = false;
        break;
    case Texture::Format::R16F:
    case Texture::Format::R32F:
        numChannels = 1;
        isHdr = true;
        break;
    case Texture::Format::RG16F:
    case Texture::Format::RG32F:
        numChannels = 2;
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

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    if (vmaCreateBuffer(vulkanBackend.globalAllocator(), &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        LogError("Could not create staging buffer for updating image with pixel-data.\n");
    }

    if (!vulkanBackend.setBufferMemoryUsingMapping(stagingAllocation, isHdr ? (uint8_t*)value_ptr(pixel) : (uint8_t*)pixels, pixelsSize)) {
        LogError("Could not set the buffer memory for the staging buffer for updating image with pixel-data.\n");
        return;
    }

    AtScopeExit cleanUpStagingBuffer([&]() {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), stagingBuffer, stagingAllocation);
    });

    if (currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = aspectMask();
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = 0;

        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        bool success = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                                    0, nullptr,
                                    0, nullptr,
                                    1, &imageBarrier);
        });
        if (!success) {
            LogError("Could not transition the image to transfer optimal layout.\n");
            return;
        }
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
        imageBarrier.subresourceRange.aspectMask = aspectMask();
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

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

void VulkanTexture::setData(const void* data, size_t size)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

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

    if (!vulkanBackend.setBufferMemoryUsingMapping(stagingAllocation, (uint8_t*)data, size)) {
        LogError("VulkanBackend::updateTexture(): could set the buffer memory for the staging buffer.\n");
        return;
    }

    AtScopeExit cleanUpStagingBuffer([&]() {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), stagingBuffer, stagingAllocation);
    });

    if (currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = aspectMask();
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        imageBarrier.srcAccessMask = 0;

        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        bool success = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &imageBarrier);
        });
        if (!success) {
            LogError("Could not transition the image to transfer optimal layout.\n");
            return;
        }
    }

    if (!vulkanBackend.copyBufferToImage(stagingBuffer, image, extent().width(), extent().height(), hasDepthFormat())) {
        LogError("Could not copy the staging buffer to the image.\n");
        return;
    }

    currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (mipmap() != Texture::Mipmap::None && extent().width() > 1 && extent().height() > 1) {
        generateMipmaps();
    } else {
        VkImageMemoryBarrier imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        {
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            imageBarrier.image = image;
            imageBarrier.subresourceRange.aspectMask = aspectMask();
            imageBarrier.subresourceRange.baseMipLevel = 0;
            imageBarrier.subresourceRange.levelCount = 1;
            imageBarrier.subresourceRange.baseArrayLayer = 0;
            imageBarrier.subresourceRange.layerCount = 1;

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

void VulkanTexture::generateMipmaps()
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (!hasMipmaps()) {
        LogError("VulkanTexture: generateMipmaps() called on texture which doesn't have space for mipmaps allocated. Ignoring request.\n");
        return;
    }

    if (currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        LogError("VulkanTexture: generateMipmaps() called on texture which currently has the layout VK_IMAGE_LAYOUT_UNDEFINED. Ignoring request.\n");
        return;
    }

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.subresourceRange.aspectMask = aspectMask();
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    uint32_t levels = mipLevels();
    int32_t mipWidth = extent().width();
    int32_t mipHeight = extent().height();

    // We have to be very general in this function..
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAccessFlags finalAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    bool success = static_cast<VulkanBackend&>(backend()).issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        // Transition mips 1-n to transfer dst optimal
        {
            VkImageMemoryBarrier initialBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            initialBarrier.image = image;
            initialBarrier.subresourceRange.aspectMask = aspectMask();
            initialBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            initialBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            initialBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            initialBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            initialBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initialBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initialBarrier.subresourceRange.baseArrayLayer = 0;
            initialBarrier.subresourceRange.layerCount = 1;
            initialBarrier.subresourceRange.baseMipLevel = 1;
            initialBarrier.subresourceRange.levelCount = levels - 1;

            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &initialBarrier);
        }

        for (uint32_t i = 1; i < levels; ++i) {

            int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
            int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

            // The 'currentLayout' keeps track of the whole image (or kind of mip0) but when we are messing
            // with it here, it will have to be different for the different mip levels.
            VkImageLayout oldLayout = (i == 1) ? currentLayout : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);

            VkImageBlit blit = {};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = aspectMask();
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
            blit.dstSubresource.aspectMask = aspectMask();
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(commandBuffer,
                           image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit,
                           VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = finalLayout;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = finalAccess;

            vkCmdPipelineBarrier(commandBuffer,
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

        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);
    });

    if (!success) {
        LogError("VulkanTexture: error while generating mipmaps\n");
    }
}

uint32_t VulkanTexture::layerCount() const
{
    switch (type()) {
    case Texture::Type::Texture2D:
        return arrayCount();
    case Texture::Type::Cubemap:
        return 6 * arrayCount();
    default:
        ASSERT_NOT_REACHED();
    }
}

VkImageAspectFlags VulkanTexture::aspectMask() const
{
    VkImageAspectFlags mask = 0u;

    if (hasDepthFormat()) {
        mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilFormat())
            mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    } else {
        mask |= VK_IMAGE_ASPECT_COLOR_BIT;
    }

    return mask;
}

VulkanRenderTarget::VulkanRenderTarget(Backend& backend, std::vector<Attachment> attachments, bool imageless, QuirkMode quirkMode)
    : RenderTarget(backend, std::move(attachments))
    , framebufferIsImageless(imageless)
    , quirkMode(quirkMode)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    std::vector<VkImageView> allAttachmentImageViews {};
    std::vector<VkAttachmentDescription> allAttachments {};

    std::vector<VkAttachmentReference> colorAttachmentRefs {};
    std::optional<VkAttachmentReference> depthAttachmentRef {};
    std::vector<VkAttachmentReference> resolveAttachmentRefs {};

    auto createAttachmentDescription = [&](Texture* genTexture, VkImageLayout finalLayout, LoadOp loadOp, StoreOp storeOp) -> uint32_t {
        ASSERT(genTexture);
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
        if (quirkMode == QuirkMode::ForPresenting)
            attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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
            uint32_t attachmentIndex = createAttachmentDescription(attachInfo.multisampleResolveTexture, finalLayout, attachInfo.loadOp, attachInfo.storeOp);

            VkAttachmentReference attachmentRef = {};
            attachmentRef.attachment = attachmentIndex;
            attachmentRef.layout = finalLayout;

            resolveAttachmentRefs.push_back(attachmentRef);
        }

        return attachmentRef;
    };

    for (const Attachment& colorAttachment : colorAttachments()) {

        ASSERT((colorAttachment.texture->isMultisampled() && colorAttachment.multisampleResolveTexture)
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

    VkDevice device = static_cast<VulkanBackend&>(backend).device();
    if (vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &compatibleRenderPass) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create render pass\n");
    }

    VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebufferCreateInfo.renderPass = compatibleRenderPass;
    framebufferCreateInfo.attachmentCount = (uint32_t)allAttachmentImageViews.size();
    framebufferCreateInfo.pAttachments = allAttachmentImageViews.data();
    framebufferCreateInfo.width = extent().width();
    framebufferCreateInfo.height = extent().height();
    framebufferCreateInfo.layers = 1;

    VkFramebufferAttachmentsCreateInfo attachmentCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO };
    std::vector<VkFramebufferAttachmentImageInfo> attachmentImageInfos {}; 
    if (framebufferIsImageless) {

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
        LogErrorAndExit("Error trying to create framebuffer\n");
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
                LogWarning("Could not set debug name for vulkan framebuffer resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_RENDER_PASS;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(compatibleRenderPass);
            nameInfo.pObjectName = renderPassName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan render pass resource.\n");
            }
        }
    }
}

VulkanBindingSet::VulkanBindingSet(Backend& backend, std::vector<ShaderBinding> bindings)
    : BindingSet(backend, std::move(bindings))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    const auto& device = static_cast<VulkanBackend&>(backend).device();

    descriptorSetLayout = createDescriptorSetLayout();

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
        descriptorPoolCreateInfo.poolSizeCount = (uint32_t)descriptorPoolSizes.size();
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
                auto& buffer = static_cast<const VulkanBuffer&>(*bindingInfo.buffers[0]);

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
                auto& buffer = static_cast<const VulkanBuffer&>(*bindingInfo.buffers[0]);

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
                    auto& vulkanBuffer = static_cast<const VulkanBuffer&>(*buffer);

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
                auto& texture = static_cast<const VulkanTexture&>(*bindingInfo.textures[0]);

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
                auto& texture = static_cast<const VulkanTexture&>(*bindingInfo.textures[0]);

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

                    auto& texture = static_cast<const VulkanTexture&>(*genTexture);

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

                ASSERT(bindingInfo.tlas);
                auto& vulkanTlas = static_cast<const VulkanTopLevelAS&>(*bindingInfo.tlas);

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

        vkUpdateDescriptorSets(device, (uint32_t)descriptorSetWrites.size(), descriptorSetWrites.data(), 0, nullptr);
    }
}

VulkanBindingSet::~VulkanBindingSet()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vkDestroyDescriptorPool(vulkanBackend.device(), descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(vulkanBackend.device(), descriptorSetLayout, nullptr);
}

void VulkanBindingSet::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        std::string descriptorSetName = name + "-descriptorSet";
        std::string descriptorPoolName = name + "-descriptorPool";
        std::string descriptorSetLayoutName = name + "-descriptorSetLayout";

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(descriptorSet);
            nameInfo.pObjectName = descriptorSetName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan descriptor set resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_DESCRIPTOR_POOL;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(descriptorPool);
            nameInfo.pObjectName = descriptorPoolName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan descriptor pool resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(descriptorSetLayout);
            nameInfo.pObjectName = descriptorSetLayoutName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan descriptor set layout resource.\n");
            }
        }
    }
}

VkDescriptorSetLayout VulkanBindingSet::createDescriptorSetLayout() const
{
    auto& vulkanBackend = static_cast<const VulkanBackend&>(backend());

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
    descriptorSetLayoutCreateInfo.bindingCount = (uint32_t)layoutBindings.size();
    descriptorSetLayoutCreateInfo.pBindings = layoutBindings.data();

    VkDescriptorSetLayout newDescriptorSetLayout;
    if (vkCreateDescriptorSetLayout(vulkanBackend.device(), &descriptorSetLayoutCreateInfo, nullptr, &newDescriptorSetLayout) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create descriptor set layout\n");
    }

    return newDescriptorSetLayout;
}

VulkanRenderState::VulkanRenderState(Backend& backend, const RenderTarget& renderTarget, VertexLayout vertexLayout,
                                     Shader shader, const StateBindings& stateBindings,
                                     Viewport viewport, BlendState blendState, RasterState rasterState, DepthState depthState, StencilState stencilState)
    : RenderState(backend, renderTarget, vertexLayout, shader, stateBindings, viewport, blendState, rasterState, depthState, stencilState)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    const auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    const auto& device = vulkanBackend.device();

    VkVertexInputBindingDescription bindingDescription = {};
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions {};
    {
        // TODO: What about multiple bindings? Just have multiple VertexLayout:s?
        uint32_t binding = 0;

        bindingDescription.binding = binding;
        bindingDescription.stride = (uint32_t)vertexLayout.packedVertexSize();
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        attributeDescriptions.reserve(vertexLayout.components().size());

        uint32_t nextLocation = 0;
        uint32_t currentOffset = 0;

        for (const VertexComponent& component : vertexLayout.components()) {

            VkVertexInputAttributeDescription description = {};
            description.binding = binding;
            description.location = nextLocation;
            description.offset = currentOffset;

            nextLocation += 1;
            currentOffset += (uint32_t)vertexComponentSize(component);

            switch (component) {
            case VertexComponent::Position2F:
            case VertexComponent::TexCoord2F:
                description.format = VK_FORMAT_R32G32_SFLOAT;
                break;
            case VertexComponent::Position3F:
            case VertexComponent::Normal3F:
                description.format = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            case VertexComponent::Tangent4F:
                description.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            default:
                ASSERT_NOT_REACHED();
            }

            attributeDescriptions.push_back(description);
        }
    }

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages {};
    {
        for (auto& file : shader.files()) {

            // TODO: Maybe don't create new modules every time? Currently they are deleted later in this function
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(file);
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

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts {};
    for (const BindingSet* bindingSet : stateBindings.orderedBindingSets()) {
        auto* vulkanBindingSet = static_cast<const VulkanBindingSet*>(bindingSet);
        descriptorSetLayouts.push_back(vulkanBindingSet->createDescriptorSetLayout());
    }

    pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    const auto& pushConstantRange = vulkanBackend.getPushConstantRangeForShader(shader);
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
    vertInputState.vertexAttributeDescriptionCount = (uint32_t)attributeDescriptions.size();
    vertInputState.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyState.primitiveRestartEnable = VK_FALSE;

    VkViewport vkViewport = {};
    vkViewport.x = fixedViewport().x;
    vkViewport.y = fixedViewport().y;
    vkViewport.width = static_cast<float>(fixedViewport().extent.width());
    vkViewport.height = static_cast<float>(fixedViewport().extent.height());
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
    multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(renderTarget.multisampling());
    multisampling.sampleShadingEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments {};
    if (blendState.enabled) {
        // TODO: Implement blending!
        ASSERT_NOT_REACHED();
    } else {
        for (const auto& attachment : renderTarget.colorAttachments()) {
            VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // NOLINT(hicpp-signed-bitwise)
            colorBlendAttachment.blendEnable = VK_FALSE;
            colorBlendAttachments.push_back(colorBlendAttachment);
        }
    }
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = (uint32_t)colorBlendAttachments.size();
    colorBlending.pAttachments = colorBlendAttachments.data();

    VkCompareOp depthCompareOp;
    switch (depthState.compareOp) {
    case DepthCompareOp::Less:
        depthCompareOp = VK_COMPARE_OP_LESS;
        break;
    case DepthCompareOp::LessThanEqual:
        depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    case DepthCompareOp::Greater:
        depthCompareOp = VK_COMPARE_OP_GREATER;
        break;
    case DepthCompareOp::GreaterThanEqual:
        depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        break;
    case DepthCompareOp::Equal:
        depthCompareOp = VK_COMPARE_OP_EQUAL;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencilState.depthTestEnable = depthState.testDepth;
    depthStencilState.depthWriteEnable = depthState.writeDepth;
    depthStencilState.depthCompareOp = depthCompareOp;
    depthStencilState.depthBoundsTestEnable = VK_FALSE;
    depthStencilState.minDepthBounds = 0.0f;
    depthStencilState.maxDepthBounds = 1.0f;

    if (stencilState.mode != StencilMode::Disabled) {
        depthStencilState.stencilTestEnable = VK_TRUE;
        switch (stencilState.mode) {
        case StencilMode::AlwaysWrite:
            // Test
            depthStencilState.front.compareOp = VK_COMPARE_OP_ALWAYS;
            depthStencilState.front.compareMask = 0x00;
            // Writing (just set to 0xff)
            depthStencilState.front.passOp = VK_STENCIL_OP_REPLACE;
            depthStencilState.front.reference = 0xff;
            depthStencilState.front.writeMask = 0xff;
            break;

        case StencilMode::PassIfZero:
            // Test
            depthStencilState.front.compareOp = VK_COMPARE_OP_EQUAL;
            depthStencilState.front.compareMask = 0xff;
            depthStencilState.front.reference = 0x00;
            // Writing (in this case, no writing)
            depthStencilState.front.passOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.failOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.depthFailOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.writeMask = 0x00;
            break;

        case StencilMode::PassIfNotZero:
            // Test
            depthStencilState.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
            depthStencilState.front.compareMask = 0xff;
            depthStencilState.front.reference = 0x00;
            // Writing (in this case, no writing)
            depthStencilState.front.passOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.failOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.depthFailOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.writeMask = 0x00;
            break;

        default:
            ASSERT_NOT_REACHED();
        }

        // For now, no separate front/back treatment supported
        depthStencilState.back = depthStencilState.front;
    } else {
        depthStencilState.stencilTestEnable = VK_FALSE;
        depthStencilState.front = {};
        depthStencilState.back = {};
    }

    VkGraphicsPipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

    // stages
    pipelineCreateInfo.stageCount = (uint32_t)shaderStages.size();
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
    auto& vulkanRenderTarget = static_cast<const VulkanRenderTarget&>(renderTarget);
    pipelineCreateInfo.renderPass = vulkanRenderTarget.compatibleRenderPass;
    pipelineCreateInfo.subpass = 0; // TODO: How should this be handled?

    // extra stuff (optional for this)
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineCreateInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(device, vulkanBackend.pipelineCache(), 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create graphics pipeline\n");
    }

    // Remove shader modules, they are no longer needed after creating the pipeline
    for (auto& stage : shaderStages) {
        vkDestroyShaderModule(device, stage.module, nullptr);
    }
}

VulkanRenderState::~VulkanRenderState()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vkDestroyPipeline(vulkanBackend.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(vulkanBackend.device(), pipelineLayout, nullptr);
}

VulkanTopLevelAS::VulkanTopLevelAS(Backend& backend, std::vector<RTGeometryInstance> inst)
    : TopLevelAS(backend, std::move(inst))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    ASSERT(vulkanBackend.hasRtxSupport());

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

void VulkanRenderState::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        std::string pipelineName = name + "-pipeline";
        std::string pipelineLayoutName = name + "-pipelineLayout";

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipeline);
            nameInfo.pObjectName = pipelineName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan graphics pipeline resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipelineLayout);
            nameInfo.pObjectName = pipelineLayoutName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan graphics pipeline layout resource.\n");
            }
        }
    }
}

VulkanTopLevelAS::~VulkanTopLevelAS()
{
    if (!hasBackend())
        return;

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vulkanBackend.rtx().vkDestroyAccelerationStructureNV(vulkanBackend.device(), accelerationStructure, nullptr);
    vkFreeMemory(vulkanBackend.device(), memory, nullptr);

    for (auto& [buffer, allocation] : associatedBuffers) {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), buffer, allocation);
    }
}

void VulkanTopLevelAS::setName(const std::string& name)
{
    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        nameInfo.objectType = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(accelerationStructure);
        nameInfo.pObjectName = name.c_str();

        if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
            LogWarning("Could not set debug name for vulkan top level acceleration structure resource.\n");
        }
    }
}

VulkanBottomLevelAS::VulkanBottomLevelAS(Backend& backend, std::vector<RTGeometry> geos)
    : BottomLevelAS(backend, std::move(geos))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    ASSERT(vulkanBackend.hasRtxSupport());

    // All geometries in a BLAS must have the same type (i.e. AABB/triangles)
    bool isTriangleBLAS = geometries().front().hasTriangles();
    for (size_t i = 1; i < geometries().size(); ++i) {
        ASSERT(geometries()[i].hasTriangles() == isTriangleBLAS);
    }

    VkBuffer transformBuffer = VK_NULL_HANDLE;
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

        if (!vulkanBackend.setBufferMemoryUsingMapping(transformBufferAllocation, (uint8_t*)transforms.data(), totalSize)) {
            LogErrorAndExit("Error trying to copy data to the bottom level acceeration structure transform buffer.\n");
        }
    }

    std::vector<VkGeometryNV> vkGeometries {};

    for (size_t geoIdx = 0; geoIdx < geometries().size(); ++geoIdx) {
        const RTGeometry& geo = geometries()[geoIdx];

        if (geo.hasTriangles()) {
            const RTTriangleGeometry& triGeo = geo.triangles();

            VkGeometryTrianglesNV triangles { VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV };

            triangles.vertexData = static_cast<const VulkanBuffer&>(triGeo.vertexBuffer).buffer;
            triangles.vertexOffset = (VkDeviceSize)triGeo.vertexOffset;
            triangles.vertexStride = (VkDeviceSize)triGeo.vertexStride;
            triangles.vertexCount = triGeo.vertexCount;
            switch (triGeo.vertexFormat) {
            case RTVertexFormat::XYZ32F:
                triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            }

            triangles.indexData = static_cast<const VulkanBuffer&>(triGeo.indexBuffer).buffer;
            triangles.indexOffset = (VkDeviceSize)triGeo.indexOffset;
            triangles.indexCount = triGeo.indexCount;
            switch (triGeo.indexType) {
            case IndexType::UInt16:
                triangles.indexType = VK_INDEX_TYPE_UINT16;
                break;
            case IndexType::UInt32:
                triangles.indexType = VK_INDEX_TYPE_UINT32;
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
            aabbs.stride = (uint32_t)aabbGeo.aabbStride;
            aabbs.aabbData = static_cast<const VulkanBuffer&>(aabbGeo.aabbBuffer).buffer;
            aabbs.numAABBs = (uint32_t)(aabbGeo.aabbBuffer.size() / aabbGeo.aabbStride);

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
    accelerationStructureInfo.geometryCount = (uint32_t)vkGeometries.size();
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
    buildInfo.geometryCount = (uint32_t)vkGeometries.size();
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

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vulkanBackend.rtx().vkDestroyAccelerationStructureNV(vulkanBackend.device(), accelerationStructure, nullptr);
    vkFreeMemory(vulkanBackend.device(), memory, nullptr);

    for (auto& [buffer, allocation] : associatedBuffers) {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), buffer, allocation);
    }
}

void VulkanBottomLevelAS::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        nameInfo.objectType = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(accelerationStructure);
        nameInfo.pObjectName = name.c_str();

        if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
            LogWarning("Could not set debug name for vulkan bottom level acceleration structure resource.\n");
        }
    }
}

VulkanRayTracingState::VulkanRayTracingState(Backend& backend, ShaderBindingTable sbt, const StateBindings& stateBindings, uint32_t maxRecursionDepth)
    : RayTracingState(backend, sbt, stateBindings, maxRecursionDepth)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    ASSERT(vulkanBackend.hasRtxSupport());

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts {};
    for (const BindingSet* bindingSet : stateBindings.orderedBindingSets()) {
        auto* vulkanBindingSet = static_cast<const VulkanBindingSet*>(bindingSet);
        descriptorSetLayouts.push_back(vulkanBindingSet->createDescriptorSetLayout());
    }

    pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    const auto& pushConstantRange = vulkanBackend.getPushConstantRangeForShader(shaderBindingTable().pseudoShader());
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

    for (const VkDescriptorSetLayout& layout : descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(vulkanBackend.device(), layout, nullptr);
    }

    std::vector<VkShaderModule> shaderModulesToRemove {};
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages {};
    std::vector<VkRayTracingShaderGroupCreateInfoNV> shaderGroups {};

    // RayGen
    {
        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(sbt.rayGen());
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

        uint32_t shaderIndex = (uint32_t)shaderStages.size();
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
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(hitGroup.closestHit());
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

            shaderGroup.closestHitShader = (uint32_t)shaderStages.size();
            shaderStages.push_back(stageCreateInfo);
            shaderModulesToRemove.push_back(shaderModule);
        }

        if (hitGroup.hasAnyHitShader()) {
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(hitGroup.anyHit());
            moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
            moduleCreateInfo.pCode = spirv.data();

            VkShaderModule shaderModule {};
            if (vkCreateShaderModule(vulkanBackend.device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                LogErrorAndExit("Error trying to create shader module for anyhit shader for ray tracing state\n");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.stage = VK_SHADER_STAGE_ANY_HIT_BIT_NV;
            stageCreateInfo.module = shaderModule;
            stageCreateInfo.pName = "main";

            shaderGroup.anyHitShader = (uint32_t)shaderStages.size();
            shaderStages.push_back(stageCreateInfo);
            shaderModulesToRemove.push_back(shaderModule);
        }

        if (hitGroup.hasIntersectionShader()) {
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(hitGroup.intersection());
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

            shaderGroup.intersectionShader = (uint32_t)shaderStages.size();
            shaderStages.push_back(stageCreateInfo);
            shaderModulesToRemove.push_back(shaderModule);
        }

        shaderGroups.push_back(shaderGroup);
    }

    // Miss shaders
    for (const ShaderFile& missShader : sbt.missShaders()) {

        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(missShader);
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

        uint32_t shaderIndex = (uint32_t)shaderStages.size();
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
    rtPipelineCreateInfo.stageCount = (uint32_t)shaderStages.size();
    rtPipelineCreateInfo.pStages = shaderStages.data();
    rtPipelineCreateInfo.groupCount = (uint32_t)shaderGroups.size();
    rtPipelineCreateInfo.pGroups = shaderGroups.data();
    rtPipelineCreateInfo.layout = pipelineLayout;

    if (vulkanBackend.rtx().vkCreateRayTracingPipelinesNV(vulkanBackend.device(), vulkanBackend.pipelineCache(), 1, &rtPipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        LogErrorAndExit("Error creating ray tracing pipeline\n");
    }

    // Remove shader modules after creating the pipeline
    for (VkShaderModule& shaderModule : shaderModulesToRemove) {
        vkDestroyShaderModule(vulkanBackend.device(), shaderModule, nullptr);
    }

    // Create buffer for the shader binding table
    {
        uint32_t sizeOfSingleHandle = vulkanBackend.rtx().properties().shaderGroupHandleSize;
        uint32_t sizeOfAllHandles = sizeOfSingleHandle * (uint32_t)shaderGroups.size();
        std::vector<std::byte> shaderGroupHandles { sizeOfAllHandles };
        if (vulkanBackend.rtx().vkGetRayTracingShaderGroupHandlesNV(vulkanBackend.device(), pipeline, 0, (uint32_t)shaderGroups.size(), sizeOfAllHandles, shaderGroupHandles.data()) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to get shader group handles for the shader binding table.\n");
        }

        // TODO: For now we don't have any data, only shader handles, but we still have to consider the alignments & strides
        uint32_t baseAlignment = vulkanBackend.rtx().properties().shaderGroupBaseAlignment;
        uint32_t sbtSize = baseAlignment * (uint32_t)shaderGroups.size();
        std::vector<std::byte> sbtData { sbtSize };

        for (uint32_t i = 0; i < shaderGroups.size(); ++i) {

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

        if (!vulkanBackend.setBufferMemoryUsingMapping(sbtBufferAllocation, (uint8_t*)sbtData.data(), sbtSize)) {
            LogErrorAndExit("Error trying to copy data to the shader binding table.\n");
        }
    }
}

VulkanRayTracingState::~VulkanRayTracingState()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), sbtBuffer, sbtBufferAllocation);
    vkDestroyPipeline(vulkanBackend.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(vulkanBackend.device(), pipelineLayout, nullptr);
}

void VulkanRayTracingState::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        std::string pipelineName = name + "-pipeline";
        std::string pipelineLayoutName = name + "-pipelineLayout";

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipeline);
            nameInfo.pObjectName = pipelineName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan ray tracing pipeline resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipelineLayout);
            nameInfo.pObjectName = pipelineLayoutName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan ray tracing pipeline layout resource.\n");
            }
        }
    }
}

VulkanComputeState::VulkanComputeState(Backend& backend, Shader shader, std::vector<BindingSet*> bindingSets)
    : ComputeState(backend, shader, bindingSets)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);

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
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(file);
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

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts {};
    for (BindingSet* bindingSet : bindingSets) {
        auto* vulkanBindingSet = static_cast<VulkanBindingSet*>(bindingSet);
        descriptorSetLayouts.push_back(vulkanBindingSet->createDescriptorSetLayout());
    }

    pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    const auto& pushConstantRange = vulkanBackend.getPushConstantRangeForShader(shader);
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

    if (vkCreateComputePipelines(vulkanBackend.device(), vulkanBackend.pipelineCache(), 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
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
                case ShaderBindingType::TextureSampler:
                    sampledTextures.push_back(texture);
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
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vkDestroyPipeline(vulkanBackend.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(vulkanBackend.device(), pipelineLayout, nullptr);
}

void VulkanComputeState::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        std::string pipelineName = name + "-pipeline";
        std::string pipelineLayoutName = name + "-pipelineLayout";

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipeline);
            nameInfo.pObjectName = pipelineName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan compute pipeline resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipelineLayout);
            nameInfo.pObjectName = pipelineLayoutName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan compute pipeline layout resource.\n");
            }
        }
    }
}
