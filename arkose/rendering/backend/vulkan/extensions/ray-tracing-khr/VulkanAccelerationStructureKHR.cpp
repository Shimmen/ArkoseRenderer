#include "VulkanAccelerationStructureKHR.h"

#include "core/parallel/ParallelFor.h"
#include "rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingKHR.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "rendering/backend/vulkan/VulkanBackend.h"
#include "rendering/backend/vulkan/VulkanBuffer.h"
#include "rendering/backend/util/UploadBuffer.h"
#include "core/Logging.h"

VulkanTopLevelASKHR::VulkanTopLevelASKHR(Backend& backend, uint32_t maxInstanceCount)
    : TopLevelAS(backend, maxInstanceCount)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    ARKOSE_ASSERT(vulkanBackend.hasRayTracingSupport());

    accelerationStructureFlags = 0u;
    accelerationStructureFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    accelerationStructureFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;

    size_t instanceBufferSize = this->maxInstanceCount() * sizeof(VkAccelerationStructureInstanceKHR);
    instanceBuffer = vulkanBackend.createBuffer(instanceBufferSize, Buffer::Usage::RTInstanceBuffer);

    // Not needed at this point, not until we actually build this
    //updateCurrentInstanceCount(static_cast<uint32_t>(initialInstances.size()));
    //auto initialInstanceData = createInstanceData(initialInstances);
    //instanceBuffer->updateData(initialInstanceData);

    VkBufferDeviceAddressInfo instanceBufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    instanceBufferDeviceAddressInfo.buffer = static_cast<VulkanBuffer&>(*instanceBuffer).buffer;
    VkDeviceAddress instanceBufferBaseAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &instanceBufferDeviceAddressInfo);

    VkAccelerationStructureGeometryInstancesDataKHR instances { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
    instances.arrayOfPointers = VK_FALSE;
    instances.data.deviceAddress = instanceBufferBaseAddress;

    VkAccelerationStructureGeometryKHR instanceGeometry { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    instanceGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    instanceGeometry.geometry.instances = instances;
    instanceGeometry.flags = 0u;

    VkAccelerationStructureBuildGeometryInfoKHR initialBuildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    initialBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    initialBuildInfo.flags = accelerationStructureFlags;
    initialBuildInfo.geometryCount = 1;
    initialBuildInfo.pGeometries = &instanceGeometry;

    uint32_t maxInstanceCnt = this->maxInstanceCount();
    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vulkanBackend.rayTracingKHR().vkGetAccelerationStructureBuildSizesKHR(vulkanBackend.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &initialBuildInfo, &maxInstanceCnt, &buildSizesInfo);

    VkDeviceSize accelerationStructureBufferSize = buildSizesInfo.accelerationStructureSize; // (use min required size)
    accelerationStructureBufferAndAllocation = vulkanBackend.rayTracingKHR().createAccelerationStructureBuffer(accelerationStructureBufferSize, true, false);
    VkBuffer accelerationStructureBuffer = accelerationStructureBufferAndAllocation.first;
    
    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationStructureCreateInfo.buffer = accelerationStructureBuffer;
    accelerationStructureCreateInfo.size = accelerationStructureBufferSize;
    accelerationStructureCreateInfo.offset = 0;

    if (vulkanBackend.rayTracingKHR().vkCreateAccelerationStructureKHR(vulkanBackend.device(), &accelerationStructureCreateInfo, nullptr, &accelerationStructure) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create top level acceleration structure");
    }

    // Create scratch buffer
    // TODO: Don't create a scratch buffer per TLAS! If we can guarantee they don't build/update at the same time a single buffer can be reused.
    {
        VkDeviceSize minScratchBufferAlignment = vulkanBackend.rayTracingKHR().accelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment;

        // NOTE: The update scratch size will generally be much smaller than the build scratch size, so we're wasting a lot by this!
        VkDeviceSize scratchBufferMinSize = ark::alignUp(std::max(buildSizesInfo.buildScratchSize, buildSizesInfo.updateScratchSize), minScratchBufferAlignment);
        scratchBufferAndAllocation = vulkanBackend.rayTracingKHR().createAccelerationStructureBuffer(scratchBufferMinSize, true, false);

        VkBufferDeviceAddressInfo bufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        bufferDeviceAddressInfo.buffer = scratchBufferAndAllocation.first;
        scratchBufferAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &bufferDeviceAddressInfo);
        scratchBufferAddress = ark::alignUp(scratchBufferAddress, minScratchBufferAlignment);
    }
}

VulkanTopLevelASKHR::~VulkanTopLevelASKHR()
{
    if (!hasBackend())
        return;

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vulkanBackend.rayTracingKHR().vkDestroyAccelerationStructureKHR(vulkanBackend.device(), accelerationStructure, nullptr);

    vmaDestroyBuffer(vulkanBackend.globalAllocator(), scratchBufferAndAllocation.first, scratchBufferAndAllocation.second);
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), accelerationStructureBufferAndAllocation.first, accelerationStructureBufferAndAllocation.second);
}

void VulkanTopLevelASKHR::setName(const std::string& name)
{
    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        nameInfo.objectType = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(accelerationStructure);
        nameInfo.pObjectName = name.c_str();

        if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
            ARKOSE_LOG(Warning, "Could not set debug name for vulkan top level acceleration structure resource.");
        }
    }
}

void VulkanTopLevelASKHR::build(VkCommandBuffer commandBuffer, AccelerationStructureBuildType buildType)
{
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    VkBufferDeviceAddressInfo instanceBufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    instanceBufferDeviceAddressInfo.buffer = static_cast<VulkanBuffer&>(*instanceBuffer).buffer;
    VkDeviceAddress instanceBufferBaseAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &instanceBufferDeviceAddressInfo);

    VkAccelerationStructureGeometryInstancesDataKHR instances { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
    instances.arrayOfPointers = VK_FALSE;
    instances.data.deviceAddress = instanceBufferBaseAddress;

    VkAccelerationStructureGeometryKHR instanceGeometry { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    instanceGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    instanceGeometry.geometry.instances = instances;
    instanceGeometry.flags = 0u;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = accelerationStructureFlags;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &instanceGeometry;

    switch (buildType) {
    case AccelerationStructureBuildType::FullBuild:
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
        buildInfo.dstAccelerationStructure = accelerationStructure;
        break;
    case AccelerationStructureBuildType::Update:
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        buildInfo.srcAccelerationStructure = accelerationStructure;
        buildInfo.dstAccelerationStructure = accelerationStructure;
        break;
    }

    buildInfo.scratchData.deviceAddress = scratchBufferAddress;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo { 0, 0, 0, 0 };
    rangeInfo.primitiveCount = instanceCount();

    VkAccelerationStructureBuildRangeInfoKHR* rangeInfosData = &rangeInfo;
    vulkanBackend.rayTracingKHR().vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &rangeInfosData);

    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanTopLevelASKHR::updateInstanceDataWithUploadBuffer(const std::vector<RTGeometryInstance>& newInstances, UploadBuffer& uploadBuffer)
{
    updateCurrentInstanceCount(static_cast<uint32_t>(newInstances.size()));
    auto updatedInstanceData = createInstanceData(newInstances);
    uploadBuffer.upload(updatedInstanceData, *instanceBuffer);
}

std::vector<VkAccelerationStructureInstanceKHR> VulkanTopLevelASKHR::createInstanceData(const std::vector<RTGeometryInstance>& instances) const
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<const VulkanBackend&>(backend());

    std::vector<VkAccelerationStructureInstanceKHR> instanceData {};
    instanceData.resize(instances.size());

    ParallelForBatched(instances.size(), 128, [&](size_t idx) {
        RTGeometryInstance const& instance = instances[idx];
        VkAccelerationStructureInstanceKHR& vkInstance = instanceData[idx];

        auto* blas = dynamic_cast<VulkanBottomLevelASKHR const*>(instance.blas);
        ARKOSE_ASSERT(blas != nullptr); // ensure we do in face have a KHR version here

        vkInstance.transform = vulkanBackend.rayTracingKHR().toVkTransformMatrixKHR(instance.transform->worldMatrix());
        vkInstance.instanceCustomIndex = instance.customInstanceId; // NOTE: This is gl_InstanceCustomIndexEXT, we should be smarter about this..
        vkInstance.accelerationStructureReference = blas->accelerationStructureDeviceAddress;
        vkInstance.flags = 0; // VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        vkInstance.mask = instance.hitMask; // Only register hit if rayMask & instance.mask != 0
        vkInstance.instanceShaderBindingTableRecordOffset = instance.shaderBindingTableOffset; // We will use the same hit group for all objects
    });

    return instanceData;
}

VulkanBottomLevelASKHR::VulkanBottomLevelASKHR(Backend& backend, std::vector<RTGeometry> geos)
    : BottomLevelAS(backend, std::move(geos))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    ARKOSE_ASSERT(vulkanBackend.hasRayTracingSupport());

    // All geometries in a BLAS must have the same type (i.e. AABB/triangles)
    bool isTriangleBLAS = geometries().front().hasTriangles();
    for (size_t i = 1; i < geometries().size(); ++i) {
        ARKOSE_ASSERT(geometries()[i].hasTriangles() == isTriangleBLAS);
    }

    // TODO: Probably don't have a single buffer per transform. It's easy enough to manage a shared one for this.
    constexpr size_t singleTransformSize = 3 * 4 * sizeof(float);
    if (isTriangleBLAS) {
        std::vector<ark::mat3x4> transforms {};
        for (auto& geo : geometries()) {
            ark::mat3x4 mat34 = transpose(geo.triangles().transform);
            transforms.push_back(mat34);
        }

        size_t totalSize = transforms.size() * singleTransformSize;
        transformBufferAndAllocation = vulkanBackend.rayTracingKHR().createAccelerationStructureBuffer(totalSize, false, true); // TODO: Can this really be read-only?

        VmaAllocationInfo allocationInfo {};
        vmaGetAllocationInfo(vulkanBackend.globalAllocator(), transformBufferAndAllocation.second, &allocationInfo);
        m_sizeInMemory += allocationInfo.size;

        if (!vulkanBackend.setBufferMemoryUsingMapping(transformBufferAndAllocation.second, (uint8_t*)transforms.data(), totalSize)) {
            ARKOSE_LOG(Fatal, "Error trying to copy data to the bottom level acceeration structure transform buffer.");
        }
    }

    VkBufferDeviceAddressInfo transformBufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    transformBufferDeviceAddressInfo.buffer = transformBufferAndAllocation.first;
    VkDeviceAddress transformBufferBaseAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &transformBufferDeviceAddressInfo);

    std::vector<uint32_t> maxPrimitiveCounts {};

    for (size_t geoIdx = 0; geoIdx < geometries().size(); ++geoIdx) {
        const RTGeometry& geo = geometries()[geoIdx];

        if (geo.hasTriangles()) {
            const RTTriangleGeometry& triGeo = geo.triangles();

            VkAccelerationStructureGeometryTrianglesDataKHR triangles { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };

            // Vertex data
            {
                VkBufferDeviceAddressInfo vertexBufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
                vertexBufferDeviceAddressInfo.buffer = static_cast<const VulkanBuffer&>(triGeo.vertexBuffer).buffer;
                VkDeviceAddress vertexBufferBaseAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &vertexBufferDeviceAddressInfo);
                
                triangles.vertexData.deviceAddress = vertexBufferBaseAddress + triGeo.vertexOffset;
                triangles.vertexStride = (VkDeviceSize)triGeo.vertexStride;
                triangles.maxVertex = triGeo.vertexCount - 1;
                switch (triGeo.vertexFormat) {
                case RTVertexFormat::XYZ32F:
                    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                    break;
                }
            }

            // Index data
            {
                VkBufferDeviceAddressInfo indexBufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
                indexBufferDeviceAddressInfo.buffer = static_cast<const VulkanBuffer&>(triGeo.indexBuffer).buffer;
                VkDeviceAddress indexBufferBaseAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &indexBufferDeviceAddressInfo);

                triangles.indexData.deviceAddress = indexBufferBaseAddress + triGeo.indexOffset;
                switch (triGeo.indexType) {
                case IndexType::UInt16:
                    triangles.indexType = VK_INDEX_TYPE_UINT16;
                    break;
                case IndexType::UInt32:
                    triangles.indexType = VK_INDEX_TYPE_UINT32;
                    break;
                }
            }

            // Transform data
            {
                triangles.transformData.deviceAddress = transformBufferBaseAddress + (geoIdx * singleTransformSize);
            }

            VkAccelerationStructureGeometryKHR geometry { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.flags = 0u; // TODO: VK_GEOMETRY_OPAQUE_BIT_KHR (we would want to signal this for actual opaque geo!
            geometry.geometry.triangles = triangles;

            VkAccelerationStructureBuildRangeInfoKHR rangeInfo;
            rangeInfo.primitiveCount = triGeo.indexCount / 3;
            rangeInfo.primitiveOffset = 0;
            rangeInfo.firstVertex = 0;
            rangeInfo.transformOffset = 0;

            vkGeometries.push_back(geometry);
            rangeInfos.push_back(rangeInfo);

            // NOTE: Right now we build a BLAS once and then forget about it, so we can assume that the current primitive count is the maximum
            uint32_t maxPrimitiveCount = rangeInfo.primitiveCount;
            maxPrimitiveCounts.push_back(maxPrimitiveCount);
        }

        else if (geo.hasAABBs()) {
            const RTAABBGeometry& aabbGeo = geo.aabbs();

            VkAccelerationStructureGeometryAabbsDataKHR aabbs { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR };

            // AABB data
            {
                VkBufferDeviceAddressInfo aabbBufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
                aabbBufferDeviceAddressInfo.buffer = static_cast<const VulkanBuffer&>(aabbGeo.aabbBuffer).buffer;
                VkDeviceAddress aabbBufferBaseAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &aabbBufferDeviceAddressInfo);

                aabbs.data.deviceAddress = aabbBufferBaseAddress; // NOTE: Assuming no offset
                aabbs.stride = (uint32_t)aabbGeo.aabbStride;
            }

            VkAccelerationStructureGeometryKHR geometry { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            geometry.flags = 0u; // TODO: VK_GEOMETRY_OPAQUE_BIT_KHR (we would want to signal this for actual opaque geo!
            geometry.geometry.aabbs = aabbs;

            VkAccelerationStructureBuildRangeInfoKHR rangeInfo;
            rangeInfo.primitiveCount = (uint32_t)(aabbGeo.aabbBuffer.size() / aabbGeo.aabbStride);
            rangeInfo.primitiveOffset = 0;
            rangeInfo.firstVertex = 0;
            rangeInfo.transformOffset = 0;

            vkGeometries.push_back(geometry);
            rangeInfos.push_back(rangeInfo);

            // NOTE: Right now we build a BLAS once and then forget about it, so we can assume that the current primitive count is the maximum
            uint32_t maxPrimitiveCount = rangeInfo.primitiveCount;
            maxPrimitiveCounts.push_back(maxPrimitiveCount);
        }
    }

    previewBuildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    previewBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    previewBuildInfo.geometryCount = (uint32_t)vkGeometries.size();
    previewBuildInfo.pGeometries = vkGeometries.data();

    // TODO/OPTIMIZATION: Don't set this for all! Pass in whether the mesh needs to be rebuilt ever (also needed for the source when we copy!)
    constexpr bool allowUpdate = true;
    if constexpr (allowUpdate) {
        previewBuildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        previewBuildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    } else {
        previewBuildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    }

    if (compactionState == CompactionState::NotCompacted) {
        previewBuildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    }

    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vulkanBackend.rayTracingKHR().vkGetAccelerationStructureBuildSizesKHR(vulkanBackend.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &previewBuildInfo, maxPrimitiveCounts.data(), &buildSizesInfo);

    VkDeviceSize accelerationStructureBufferSize = buildSizesInfo.accelerationStructureSize; // (use min required size)
    blasBufferAndAllocation = vulkanBackend.rayTracingKHR().createAccelerationStructureBuffer(accelerationStructureBufferSize, true, false);
    VkBuffer accelerationStructureBuffer = blasBufferAndAllocation.first;

    VmaAllocationInfo allocationInfo {};
    vmaGetAllocationInfo(vulkanBackend.globalAllocator(), blasBufferAndAllocation.second, &allocationInfo);
    m_sizeInMemory += allocationInfo.size;

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    accelerationStructureCreateInfo.buffer = accelerationStructureBuffer;
    accelerationStructureCreateInfo.size = accelerationStructureBufferSize;
    accelerationStructureCreateInfo.offset = 0;

    if (vulkanBackend.rayTracingKHR().vkCreateAccelerationStructureKHR(vulkanBackend.device(), &accelerationStructureCreateInfo, nullptr, &accelerationStructure) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create bottom level acceleration structure");
    }

    VkAccelerationStructureDeviceAddressInfoKHR accelerationStructureDeviceAddressInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    accelerationStructureDeviceAddressInfo.accelerationStructure = accelerationStructure;
    accelerationStructureDeviceAddress = vulkanBackend.rayTracingKHR().vkGetAccelerationStructureDeviceAddressKHR(vulkanBackend.device(), &accelerationStructureDeviceAddressInfo);

    // Create query pool for compacted size
    {
        VkQueryPoolCreateInfo compactionQueryPoolCreateInfo { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        compactionQueryPoolCreateInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        compactionQueryPoolCreateInfo.queryCount = 1;
        if (vkCreateQueryPool(vulkanBackend.device(), &compactionQueryPoolCreateInfo, nullptr, &compactionQueryPool) != VK_SUCCESS) {
            ARKOSE_LOG(Warning, "Error trying to create query pool for BLAS compaction size, ignoring");
        }
    }

    // Create scratch buffer
    // TODO: Don't create a scratch buffer per BLAS! If we can guarantee they don't build/update at the same time a single buffer can be reused.
    {
        VkDeviceSize minScratchBufferAlignment = vulkanBackend.rayTracingKHR().accelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment;

        // NOTE: The update scratch size will generally be much smaller than the build scratch size, so we're wasting a lot by this!
        VkDeviceSize scratchBufferMinSize = ark::alignUp(std::max(buildSizesInfo.buildScratchSize, buildSizesInfo.updateScratchSize), minScratchBufferAlignment);
        scratchBufferAndAllocation = vulkanBackend.rayTracingKHR().createAccelerationStructureBuffer(scratchBufferMinSize, true, false);

        VkBufferDeviceAddressInfo bufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        bufferDeviceAddressInfo.buffer = scratchBufferAndAllocation.first;
        scratchBufferAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &bufferDeviceAddressInfo);
        scratchBufferAddress = ark::alignUp(scratchBufferAddress, minScratchBufferAlignment);
    }
}

VulkanBottomLevelASKHR::~VulkanBottomLevelASKHR()
{
    if (!hasBackend())
        return;

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    vkDestroyQueryPool(vulkanBackend.device(), compactionQueryPool, nullptr);
    vulkanBackend.rayTracingKHR().vkDestroyAccelerationStructureKHR(vulkanBackend.device(), accelerationStructure, nullptr);

    vmaDestroyBuffer(vulkanBackend.globalAllocator(), blasBufferAndAllocation.first, blasBufferAndAllocation.second);
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), scratchBufferAndAllocation.first, scratchBufferAndAllocation.second);
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), transformBufferAndAllocation.first, transformBufferAndAllocation.second);
}

void VulkanBottomLevelASKHR::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        nameInfo.objectType = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(accelerationStructure);
        nameInfo.pObjectName = name.c_str();

        if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
            ARKOSE_LOG(Warning, "Could not set debug name for vulkan bottom level acceleration structure resource.");
        }
    }
}

void VulkanBottomLevelASKHR::build(VkCommandBuffer commandBuffer, AccelerationStructureBuildType buildType)
{
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = previewBuildInfo;
    buildInfo.scratchData.deviceAddress = scratchBufferAddress;

    switch (buildType) {
    case AccelerationStructureBuildType::FullBuild:
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
        buildInfo.dstAccelerationStructure = accelerationStructure;
        break;
    case AccelerationStructureBuildType::Update:
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        buildInfo.srcAccelerationStructure = accelerationStructure;
        buildInfo.dstAccelerationStructure = accelerationStructure;
        break;
    }

    VkAccelerationStructureBuildRangeInfoKHR* rangeInfosData = rangeInfos.data();
    vulkanBackend.rayTracingKHR().vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &rangeInfosData);

    bool allowCompaction = previewBuildInfo.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    if (allowCompaction && compactionQueryPool && compactionState == CompactionState::NotCompacted) {
        vkCmdResetQueryPool(commandBuffer, compactionQueryPool, 0, 1);
        vulkanBackend.rayTracingKHR().vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer,
                                                                                    1, &accelerationStructure,
                                                                                    VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                                                                                    compactionQueryPool, 0);
        compactionState = CompactionState::CompactSizeRequested;
    }
}

void VulkanBottomLevelASKHR::copyFrom(VkCommandBuffer commandBuffer, VulkanBottomLevelASKHR const& copySource)
{
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    VkCopyAccelerationStructureInfoKHR copyInfo { VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
    copyInfo.src = static_cast<VulkanBottomLevelASKHR const&>(copySource).accelerationStructure;
    copyInfo.dst = accelerationStructure;

    vulkanBackend.rayTracingKHR().vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyInfo);
}

bool VulkanBottomLevelASKHR::compact(VkCommandBuffer commandBuffer)
{
    ARKOSE_ASSERT(compactionQueryPool != VK_NULL_HANDLE);
    ARKOSE_ASSERT(compactionState == CompactionState::CompactSizeRequested);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    //
    // Read compacted size
    //

    u32 compactBlasSize;
    VkResult result = vkGetQueryPoolResults(vulkanBackend.device(), compactionQueryPool, 0, 1, sizeof(compactBlasSize), &compactBlasSize, sizeof(compactBlasSize), 0);

    if (result == VK_SUCCESS) {
        //f32 compactionPercentage = 100.0f * static_cast<f32>(compactBlasSize) / static_cast<f32>(sizeInMemory());
        //ARKOSE_LOG(Info, "Will compact BLAS to {:.1f} of its original size", compactionPercentage);
    } else if (result == VK_NOT_READY) {
        //ARKOSE_LOG(Info, "BLAS compaction not yet ready!");
        return false;
    } else {
        ARKOSE_LOG(Error, "Failed to read BLAS compaction size from the query pool, now sure how this could have happened...");
        return false;
    }

    //
    // Make smaller acceleration structure to compact into
    //

    auto compactBlasBufferAndAllocation = vulkanBackend.rayTracingKHR().createAccelerationStructureBuffer(compactBlasSize, true, false);
    VkBuffer compactBlasBuffer = compactBlasBufferAndAllocation.first;

    VkAccelerationStructureCreateInfoKHR compactBlasStructureCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    compactBlasStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    compactBlasStructureCreateInfo.buffer = compactBlasBuffer;
    compactBlasStructureCreateInfo.size = compactBlasSize;
    compactBlasStructureCreateInfo.offset = 0;

    VkAccelerationStructureKHR compactAccelerationStructure;
    if (vulkanBackend.rayTracingKHR().vkCreateAccelerationStructureKHR(vulkanBackend.device(), &compactBlasStructureCreateInfo, nullptr, &compactAccelerationStructure) != VK_SUCCESS) {
        ARKOSE_LOG(Error, "Error trying to create compact bottom level acceleration structure");
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), compactBlasBufferAndAllocation.first, compactBlasBufferAndAllocation.second);
        return false;
    }

    //
    // Do compaction
    //

    VkCopyAccelerationStructureInfoKHR copyInfo { VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
    copyInfo.src = accelerationStructure;
    copyInfo.dst = compactAccelerationStructure;

    vulkanBackend.rayTracingKHR().vkCmdCopyAccelerationStructureKHR(commandBuffer, &copyInfo);

    //
    // Enqueue old, uncompacted BLAS for deletion
    //

    VkAccelerationStructureKHR blasToDestroy = accelerationStructure;
    std::pair<VkBuffer, VmaAllocation> blasBufferAndAllocationToFree = blasBufferAndAllocation;

    vulkanBackend.enqueueForDeletion(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, blasToDestroy, VK_NULL_HANDLE);
    vulkanBackend.enqueueForDeletion(VK_OBJECT_TYPE_BUFFER, blasBufferAndAllocationToFree.first, blasBufferAndAllocationToFree.second);

    //
    // Swap BLASs
    //

    accelerationStructure = compactAccelerationStructure;
    blasBufferAndAllocation = compactBlasBufferAndAllocation;

    VkAccelerationStructureDeviceAddressInfoKHR blasDeviceAddressInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    blasDeviceAddressInfo.accelerationStructure = accelerationStructure;
    accelerationStructureDeviceAddress = vulkanBackend.rayTracingKHR().vkGetAccelerationStructureDeviceAddressKHR(vulkanBackend.device(), &blasDeviceAddressInfo);

    compactionState = CompactionState::Compacted;

    return true;
}
