#include "VulkanBuffer.h"

#include "rendering/backend/vulkan/VulkanBackend.h"
#include "core/Logging.h"
#include "utility/Profiling.h"

VulkanBuffer::VulkanBuffer(Backend& backend, size_t size, Usage usage)
    : Buffer(backend, size, usage)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();
    createInternal(size, buffer, allocation, allocationInfo);
}

VulkanBuffer::~VulkanBuffer()
{
    destroyInternal(buffer, allocation, allocationInfo);
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
            ARKOSE_LOG(Warning, "Could not set debug name for vulkan buffer resource.");
        }
    }
}

bool VulkanBuffer::mapData(MapMode mapMode, size_t size, size_t offset, std::function<void(std::byte*)>&& mapCallback)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    ARKOSE_ASSERT(size > 0);
    ARKOSE_ASSERT(offset + size <= m_size);

    switch (usage()) {
    case Buffer::Usage::Upload:
        if (mapMode == MapMode::Read) {
            ARKOSE_LOG(Warning, "Mapping an upload buffer for reading - this can be prohibitively slow and is not recommended!");
        }
        break;
    case Buffer::Usage::Readback:
        if (mapMode == MapMode::Write) {
            ARKOSE_LOG(Warning, "Mapping a readback buffer for writing - this can be prohibitively slow and is not recommended!");
        }
        break;
    default:
        ARKOSE_LOG(Error, "Can only mapData from an Upload or Readback buffer, ignoring.");
        return false;
    }

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    ARKOSE_ASSERT(allocationInfo.pMappedData != nullptr); // should be persistently mapped!

    std::byte* baseAddress = reinterpret_cast<std::byte*>(allocationInfo.pMappedData);
    std::byte* requestedAddress = baseAddress + offset;

    VkMappedMemoryRange mappedMemoryRange = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
    mappedMemoryRange.memory = allocationInfo.deviceMemory;
    mappedMemoryRange.offset = allocationInfo.offset + offset;
    mappedMemoryRange.size = size;

    VkMemoryType const& mappedMemoryType = vulkanBackend.physicalDeviceMemoryProperties().memoryTypes[allocationInfo.memoryType];
    ARKOSE_ASSERT(mappedMemoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    bool hostCoherentMemory = (mappedMemoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    if (!hostCoherentMemory) {
        switch (mapMode) {
        case MapMode::Read:
        case MapMode::ReadWrite:
            vkInvalidateMappedMemoryRanges(vulkanBackend.device(), 1, &mappedMemoryRange);
            break;
        case MapMode::Write:
            break;
        }
    }

    mapCallback(requestedAddress);

    if (!hostCoherentMemory) {
        switch (mapMode) {
        case MapMode::Write:
        case MapMode::ReadWrite:
            vkFlushMappedMemoryRanges(vulkanBackend.device(), 1, &mappedMemoryRange);
            break;
        case MapMode::Read:
            break;
        }
    }

    return true;
}

void VulkanBuffer::updateData(const std::byte* data, size_t updateSize, size_t offset)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (updateSize == 0) {
        return;
    }
    if (offset + updateSize > size()) {
        ARKOSE_LOG(Fatal, "Attempt at updating buffer outside of bounds, exiting.");
    }

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    switch (usage()) {
    case Buffer::Usage::Upload:
        if (!vulkanBackend.setBufferMemoryUsingMapping(allocation, (uint8_t*)data, updateSize, offset)) {
            ARKOSE_LOG(Error, "Failed to update the data of upload buffer.");
        }
        break;
    case Buffer::Usage::Readback:
        ARKOSE_LOG(Error, "Can't update buffer with Readback memory hint, ignoring.");
        break;
    default:
        if (!vulkanBackend.setBufferDataUsingStagingBuffer(buffer, (uint8_t*)data, updateSize, offset)) {
            ARKOSE_LOG(Error, "Failed to update the data of buffer");
        }
        break;
    }
}

void VulkanBuffer::reallocateWithSize(size_t newSize, ReallocateStrategy strategy)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (strategy == ReallocateStrategy::CopyExistingData && newSize < size())
        ARKOSE_LOG(Fatal, "Can't reallocate buffer ReallocateStrategy::CopyExistingData if the new size is smaller than the current size!");

    switch (strategy) {
    case ReallocateStrategy::DiscardExistingData:

        destroyInternal(buffer, allocation, allocationInfo);
        createInternal(newSize, buffer, allocation, allocationInfo);
        this->m_size = newSize;

        break;

    case ReallocateStrategy::CopyExistingData:

        VkBuffer newBuffer;
        VmaAllocation newAllocation;
        createInternal(newSize, newBuffer, newAllocation, allocationInfo);

        auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
        vulkanBackend.copyBuffer(buffer, newBuffer, size());

        destroyInternal(buffer, allocation, allocationInfo);

        buffer = newBuffer;
        allocation = newAllocation;
        this->m_size = newSize;

        break;
    }

    // Re-set GPU buffer name for the new resource
    if (!name().empty())
        setName(name());
}

void VulkanBuffer::createInternal(size_t size, VkBuffer& outBuffer, VmaAllocation& outAllocation, VmaAllocationInfo& outAllocationInfo)
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

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    VkBufferUsageFlags usageFlags = 0u;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    switch (usage()) {
    case Buffer::Usage::Vertex:
        usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        break;
    case Buffer::Usage::Index:
        usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        break;
    case Buffer::Usage::RTInstanceBuffer:
        usageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        if constexpr (vulkanDebugMode) {
            usageFlags |= VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
        }
        break;
    case Buffer::Usage::ConstantBuffer:
        usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        break;
    case Buffer::Usage::StorageBuffer:
        usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        break;
    case Buffer::Usage::IndirectBuffer:
        usageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        break;
    case Buffer::Usage::Upload:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU; // (ensures host visible)
        allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case Buffer::Usage::Readback:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU; // (ensures host visible)
        //allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; // ??
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    // Let all buffers be valid as transfer source & destination - I can't think of many times when
    // we don't need it, and I also can't think of any hardware where this could make a difference.
    // Hopefully it wont be a problem :^)
    usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (storageCapable()) {
        usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }

    // Make vertex & index buffers also be usable in ray tracing acceleration structures
    if (usage() == Buffer::Usage::Vertex || usage() == Buffer::Usage::Index) {
        if (vulkanBackend.hasRayTracingSupport()) {
            usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            usageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            if constexpr (vulkanDebugMode) {
                usageFlags |= VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
            }
        }
    }

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.size = bufferSize;
    bufferCreateInfo.usage = usageFlags;

    auto& allocator = static_cast<VulkanBackend&>(backend()).globalAllocator();

    if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &outBuffer, &outAllocation, &outAllocationInfo) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Could not create buffer of size {}.", size);
    }

    m_sizeInMemory = allocationInfo.size;
}

void VulkanBuffer::destroyInternal(VkBuffer inBuffer, VmaAllocation inAllocation, VmaAllocationInfo& inAllocationInfo)
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), inBuffer, inAllocation);
    inAllocationInfo = {};
}
