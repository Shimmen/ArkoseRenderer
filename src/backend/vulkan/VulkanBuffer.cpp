#include "VulkanBuffer.h"

#include "backend/vulkan/VulkanBackend.h"
#include "utility/Profiling.h"
#include "utility/Logging.h"

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