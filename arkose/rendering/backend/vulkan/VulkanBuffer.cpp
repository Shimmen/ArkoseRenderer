#include "VulkanBuffer.h"

#include "rendering/backend/vulkan/VulkanBackend.h"
#include "core/Logging.h"
#include "utility/Profiling.h"

VulkanBuffer::VulkanBuffer(Backend& backend, size_t size, Usage usage)
    : Buffer(backend, size, usage)
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
            ARKOSE_LOG(Warning, "Could not set debug name for vulkan buffer resource.");
        }
    }
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
        switch (vulkanBackend.rayTracingBackend()) {
        case VulkanBackend::RayTracingBackend::NvExtension:
            usageFlags |= VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
            break;
        case VulkanBackend::RayTracingBackend::KhrExtension:
            usageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            if constexpr (vulkanDebugMode) {
                usageFlags |= VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
            }
            break;
        default:
            ASSERT_NOT_REACHED();
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
        break;
    case Buffer::Usage::Readback:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU; // (ensures host visible)
        //allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; // ??
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
        switch (vulkanBackend.rayTracingBackend()) {
        case VulkanBackend::RayTracingBackend::KhrExtension:
            usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            usageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            if constexpr (vulkanDebugMode) {
                usageFlags |= VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
            }
            break;
        default:
            // no action needed
            break;
        }
    }

    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.size = bufferSize;
    bufferCreateInfo.usage = usageFlags;

    auto& allocator = static_cast<VulkanBackend&>(backend()).globalAllocator();

    VmaAllocationInfo allocationInfo;
    if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &outBuffer, &outAllocation, &allocationInfo) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Could not create buffer of size {}.", size);
    }

    m_sizeInMemory = allocationInfo.size;
}

void VulkanBuffer::destroyInternal(VkBuffer inBuffer, VmaAllocation inAllocation)
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), inBuffer, inAllocation);
}
