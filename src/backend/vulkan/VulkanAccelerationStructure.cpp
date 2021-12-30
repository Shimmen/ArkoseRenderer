#include "VulkanAccelerationStructure.h"

#include "backend/vulkan/VulkanBackend.h"
#include "backend/shader/ShaderManager.h"

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
