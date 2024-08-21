#include "VulkanAccelerationStructureNV.h"

#include "rendering/backend/vulkan/VulkanBackend.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "rendering/backend/util/UploadBuffer.h"
#include "core/Logging.h"

VulkanTopLevelASNV::VulkanTopLevelASNV(Backend& backend, uint32_t maxInstanceCount, std::vector<RTGeometryInstance> initialInstances)
    : TopLevelAS(backend, maxInstanceCount)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    ARKOSE_ASSERT(vulkanBackend.hasRayTracingSupport());

    // Something more here maybe? Like fast to build/traverse, can be compacted, etc.
    accelerationStructureFlags = 0u;
    accelerationStructureFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV;
    accelerationStructureFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;

    VkAccelerationStructureInfoNV accelerationStructureInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    accelerationStructureInfo.flags = accelerationStructureFlags;
    accelerationStructureInfo.instanceCount = this->maxInstanceCount();
    accelerationStructureInfo.geometryCount = 0;

    VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV };
    accelerationStructureCreateInfo.info = accelerationStructureInfo;
    if (vulkanBackend.rayTracingNV().vkCreateAccelerationStructureNV(vulkanBackend.device(), &accelerationStructureCreateInfo, nullptr, &accelerationStructure) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create top level acceleration structure");
    }

    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
    memoryRequirementsInfo.accelerationStructure = accelerationStructure;
    VkMemoryRequirements2 memoryRequirements2 {};
    vulkanBackend.rayTracingNV().vkGetAccelerationStructureMemoryRequirementsNV(vulkanBackend.device(), &memoryRequirementsInfo, &memoryRequirements2);

    // See https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/issues/63#issuecomment-501246981
    VmaAllocationInfo allocationInfo;
    VmaAllocationCreateInfo allocationCreateInfo {};
    allocationCreateInfo.memoryTypeBits = memoryRequirements2.memoryRequirements.memoryTypeBits;
    if (vmaAllocateMemory(vulkanBackend.globalAllocator(), &memoryRequirements2.memoryRequirements, &allocationCreateInfo, &allocation, &allocationInfo) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create allocate memory for acceleration structure");
    }

    VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo { VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV };
    accelerationStructureMemoryInfo.accelerationStructure = accelerationStructure;
    accelerationStructureMemoryInfo.memory = allocationInfo.deviceMemory;
    accelerationStructureMemoryInfo.memoryOffset = allocationInfo.offset;
    if (vulkanBackend.rayTracingNV().vkBindAccelerationStructureMemoryNV(vulkanBackend.device(), 1, &accelerationStructureMemoryInfo) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to bind memory to acceleration structure");
    }

    if (vulkanBackend.rayTracingNV().vkGetAccelerationStructureHandleNV(vulkanBackend.device(), accelerationStructure, sizeof(uint64_t), &handle) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to get acceleration structure handle");
    }

    size_t instanceBufferSize = this->maxInstanceCount() * sizeof(VulkanRayTracingNV::GeometryInstance);
    instanceBuffer = vulkanBackend.createBuffer(instanceBufferSize, Buffer::Usage::RTInstanceBuffer);

    if (initialInstances.size() > 0) {
        updateCurrentInstanceCount(static_cast<uint32_t>(initialInstances.size()));

        auto initialInstanceData = vulkanBackend.rayTracingNV().createInstanceData(initialInstances);
        instanceBuffer->updateData(initialInstanceData.data(), initialInstanceData.size() * sizeof(VulkanRayTracingNV::GeometryInstance));

        bool buildSuccess = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            build(commandBuffer, AccelerationStructureBuildType::FullBuild);
        });
        if (!buildSuccess) {
            ARKOSE_LOG(Fatal, "Error trying to build top level acceleration structure (initial build)");
        }
    }
}

VulkanTopLevelASNV::~VulkanTopLevelASNV()
{
    if (!hasBackend())
        return;

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vulkanBackend.rayTracingNV().vkDestroyAccelerationStructureNV(vulkanBackend.device(), accelerationStructure, nullptr);
    vmaFreeMemory(vulkanBackend.globalAllocator(), allocation);
}

void VulkanTopLevelASNV::setName(const std::string& name)
{
    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        nameInfo.objectType = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(accelerationStructure);
        nameInfo.pObjectName = name.c_str();

        if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
            ARKOSE_LOG(Warning, "Could not set debug name for vulkan top level acceleration structure resource.");
        }
    }
}

void VulkanTopLevelASNV::build(VkCommandBuffer commandBuffer, AccelerationStructureBuildType buildType)
{
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    VkBuffer vkInstanceBuffer = static_cast<VulkanBuffer&>(*instanceBuffer).buffer;

    VkAccelerationStructureInfoNV buildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    buildInfo.flags = accelerationStructureFlags;
    buildInfo.instanceCount = instanceCount();
    buildInfo.geometryCount = 0;
    buildInfo.pGeometries = nullptr;

    VkBuffer scratchBuffer;
    VmaAllocation scratchAllocation;

    switch (buildType) {
    case AccelerationStructureBuildType::FullBuild:
        scratchBuffer= vulkanBackend.rayTracingNV().createScratchBufferForAccelerationStructure(accelerationStructure, false, scratchAllocation);
        vulkanBackend.rayTracingNV().vkCmdBuildAccelerationStructureNV(
            commandBuffer,
            &buildInfo,
            vkInstanceBuffer, 0,
            VK_FALSE,
            accelerationStructure,
            VK_NULL_HANDLE,
            scratchBuffer, 0);
        break;
    case AccelerationStructureBuildType::Update:
        scratchBuffer = vulkanBackend.rayTracingNV().createScratchBufferForAccelerationStructure(accelerationStructure, true, scratchAllocation);
        vulkanBackend.rayTracingNV().vkCmdBuildAccelerationStructureNV(
            commandBuffer,
            &buildInfo,
            vkInstanceBuffer, 0,
            VK_TRUE,
            accelerationStructure,
            accelerationStructure,
            scratchBuffer, 0);
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    // TODO: Maybe don't throw the allocation away (when building the first time), so we can reuse it here?
    //  However, it may be a different size, though! So maybe not. Or if we use the max(build, rebuild) size?
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), scratchBuffer, scratchAllocation);
}

void VulkanTopLevelASNV::updateInstanceDataWithUploadBuffer(const std::vector<RTGeometryInstance>& newInstances, UploadBuffer& uploadBuffer)
{
    updateCurrentInstanceCount(static_cast<uint32_t>(newInstances.size()));
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    auto updatedInstanceData = vulkanBackend.rayTracingNV().createInstanceData(newInstances);
    uploadBuffer.upload(updatedInstanceData, *instanceBuffer);
}

VulkanBottomLevelASNV::VulkanBottomLevelASNV(Backend& backend, std::vector<RTGeometry> geos, BottomLevelAS const* copySource)
    : BottomLevelAS(backend, std::move(geos))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (copySource != nullptr) {
        ARKOSE_LOG(Fatal, "Creating a BLAS from a copy source is currently only implemented for the KHR extention!");
    }

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    ARKOSE_ASSERT(vulkanBackend.hasRayTracingSupport());

    // All geometries in a BLAS must have the same type (i.e. AABB/triangles)
    bool isTriangleBLAS = geometries().front().hasTriangles();
    for (size_t i = 1; i < geometries().size(); ++i) {
        ARKOSE_ASSERT(geometries()[i].hasTriangles() == isTriangleBLAS);
    }

    VkBuffer transformBuffer = VK_NULL_HANDLE;
    VmaAllocation transformBufferAllocation;
    size_t singleTransformSize = 3 * 4 * sizeof(float);
    if (isTriangleBLAS) {
        std::vector<ark::mat3x4> transforms {};
        for (auto& geo : geometries()) {
            ark::mat3x4 mat34 = transpose(geo.triangles().transform);
            transforms.push_back(mat34);
        }

        size_t totalSize = transforms.size() * singleTransformSize;

        VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferCreateInfo.usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV; // (I can't find info on usage from the spec, but I assume this should work)
        bufferCreateInfo.size = totalSize;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(vulkanBackend.globalAllocator(), &bufferCreateInfo, &allocCreateInfo, &transformBuffer, &transformBufferAllocation, &allocationInfo) != VK_SUCCESS) {
            ARKOSE_LOG(Fatal, "Error trying to create buffer for the bottom level acceeration structure transforms.");
        }

        if (!vulkanBackend.setBufferMemoryUsingMapping(transformBufferAllocation, (uint8_t*)transforms.data(), totalSize)) {
            ARKOSE_LOG(Fatal, "Error trying to copy data to the bottom level acceeration structure transform buffer.");
        }

        m_sizeInMemory += allocationInfo.size;
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
    if (vulkanBackend.rayTracingNV().vkCreateAccelerationStructureNV(vulkanBackend.device(), &accelerationStructureCreateInfo, nullptr, &accelerationStructure) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create bottom level acceleration structure");
    }

    VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV };
    memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
    memoryRequirementsInfo.accelerationStructure = accelerationStructure;
    VkMemoryRequirements2 memoryRequirements2 {};
    vulkanBackend.rayTracingNV().vkGetAccelerationStructureMemoryRequirementsNV(vulkanBackend.device(), &memoryRequirementsInfo, &memoryRequirements2);

    // See https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/issues/63#issuecomment-501246981
    VmaAllocationInfo allocationInfo;
    VmaAllocationCreateInfo allocationCreateInfo {};
    allocationCreateInfo.memoryTypeBits = memoryRequirements2.memoryRequirements.memoryTypeBits;
    if (vmaAllocateMemory(vulkanBackend.globalAllocator(), &memoryRequirements2.memoryRequirements, &allocationCreateInfo, &allocation, &allocationInfo) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create allocate memory for acceleration structure");
    }

    m_sizeInMemory += allocationInfo.size;

    VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo { VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV };
    accelerationStructureMemoryInfo.accelerationStructure = accelerationStructure;
    accelerationStructureMemoryInfo.memory = allocationInfo.deviceMemory;
    accelerationStructureMemoryInfo.memoryOffset = allocationInfo.offset;
    if (vulkanBackend.rayTracingNV().vkBindAccelerationStructureMemoryNV(vulkanBackend.device(), 1, &accelerationStructureMemoryInfo) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to bind memory to acceleration structure");
    }

    if (vulkanBackend.rayTracingNV().vkGetAccelerationStructureHandleNV(vulkanBackend.device(), accelerationStructure, sizeof(uint64_t), &handle) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to get acceleration structure handle");
    }

    VmaAllocation scratchAllocation;
    VkBuffer scratchBuffer = vulkanBackend.rayTracingNV().createScratchBufferForAccelerationStructure(accelerationStructure, false, scratchAllocation);

    VkAccelerationStructureInfoNV buildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
    buildInfo.geometryCount = (uint32_t)vkGeometries.size();
    buildInfo.pGeometries = vkGeometries.data();

    vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        vulkanBackend.rayTracingNV().vkCmdBuildAccelerationStructureNV(
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

VulkanBottomLevelASNV::~VulkanBottomLevelASNV()
{
    if (!hasBackend())
        return;

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vulkanBackend.rayTracingNV().vkDestroyAccelerationStructureNV(vulkanBackend.device(), accelerationStructure, nullptr);
    vmaFreeMemory(vulkanBackend.globalAllocator(), allocation);

    for (auto& [buffer, allocation] : associatedBuffers) {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), buffer, allocation);
    }
}

void VulkanBottomLevelASNV::setName(const std::string& name)
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
            ARKOSE_LOG(Warning, "Could not set debug name for vulkan bottom level acceleration structure resource.");
        }
    }
}
