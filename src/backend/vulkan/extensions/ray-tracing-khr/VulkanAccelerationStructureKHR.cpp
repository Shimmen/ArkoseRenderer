#include "VulkanAccelerationStructureKHR.h"

#include "backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingKHR.h"
#include "backend/shader/ShaderManager.h"
#include "backend/vulkan/VulkanBackend.h"

VulkanTopLevelASKHR::VulkanTopLevelASKHR(Backend& backend, uint32_t maxInstanceCount, std::vector<RTGeometryInstance> initialInstances)
    : TopLevelAS(backend, maxInstanceCount)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    ASSERT(vulkanBackend.hasRayTracingSupport());

    accelerationStructureFlags = 0u;
    accelerationStructureFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV;
    accelerationStructureFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;

    size_t instanceBufferSize = this->maxInstanceCount() * sizeof(VkAccelerationStructureInstanceKHR);
    instanceBuffer = vulkanBackend.createBuffer(instanceBufferSize, Buffer::Usage::RTInstanceBuffer, Buffer::MemoryHint::GpuOptimal);

    updateCurrentInstanceCount(static_cast<uint32_t>(initialInstances.size()));
    auto initialInstanceData = createInstanceData(initialInstances);
    instanceBuffer->updateData(initialInstanceData);

    VkBufferDeviceAddressInfo instanceBufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    instanceBufferDeviceAddressInfo.buffer = static_cast<VulkanBuffer&>(*instanceBuffer).buffer;
    VkDeviceAddress instanceBufferBaseAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &instanceBufferDeviceAddressInfo);

    // TODO: What is the point of having the build info for this?? We just want to know the required size of the acceleration structure? Just the max count should suffice?
    // TODO: What is the point of having the build info for this?? We just want to know the required size of the acceleration structure? Just the max count should suffice?
    // TODO: What is the point of having the build info for this?? We just want to know the required size of the acceleration structure? Just the max count should suffice?
    // TODO: What is the point of having the build info for this?? We just want to know the required size of the acceleration structure? Just the max count should suffice?

    // TODO: And if we do need it, we probably also need to set valid data in it before we check sizes?? Right?! Otherwise this doesn't make much sense.
    // TODO: And if we do need it, we probably also need to set valid data in it before we check sizes?? Right?! Otherwise this doesn't make much sense.
    // TODO: And if we do need it, we probably also need to set valid data in it before we check sizes?? Right?! Otherwise this doesn't make much sense.
    // TODO: And if we do need it, we probably also need to set valid data in it before we check sizes?? Right?! Otherwise this doesn't make much sense.

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
    ASSERT(buildSizesInfo.buildScratchSize <= VulkanRayTracingKHR::SharedScratchBufferSize);
    ASSERT(buildSizesInfo.updateScratchSize <= VulkanRayTracingKHR::SharedScratchBufferSize);

    VkDeviceSize accelerationStructureBufferSize = buildSizesInfo.accelerationStructureSize; // (use min required size)
    accelerationStructureBufferAndAllocation = vulkanBackend.rayTracingKHR().createAccelerationStructureBuffer(accelerationStructureBufferSize, true, false);
    VkBuffer accelerationStructureBuffer = accelerationStructureBufferAndAllocation.first;
    
    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationStructureCreateInfo.buffer = accelerationStructureBuffer;
    accelerationStructureCreateInfo.size = accelerationStructureBufferSize;
    accelerationStructureCreateInfo.offset = 0;

    if (vulkanBackend.rayTracingKHR().vkCreateAccelerationStructureKHR(vulkanBackend.device(), &accelerationStructureCreateInfo, nullptr, &accelerationStructure) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create top level acceleration structure\n");
    }

    VkAccelerationStructureDeviceAddressInfoKHR accelerationStructureDeviceAddressInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    accelerationStructureDeviceAddressInfo.accelerationStructure = accelerationStructure;
    accelerationStructureDeviceAddress = vulkanBackend.rayTracingKHR().vkGetAccelerationStructureDeviceAddressKHR(vulkanBackend.device(), &accelerationStructureDeviceAddressInfo);

    bool buildSuccess = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer cmdBuffer) {
        build(cmdBuffer, BuildType::BuildInitial);
    });
    if (!buildSuccess) {
        LogErrorAndExit("Error trying to build top level acceleration structure\n");
    }
}

VulkanTopLevelASKHR::~VulkanTopLevelASKHR()
{
    if (!hasBackend())
        return;

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vulkanBackend.rayTracingKHR().vkDestroyAccelerationStructureKHR(vulkanBackend.device(), accelerationStructure, nullptr);

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
            LogWarning("Could not set debug name for vulkan top level acceleration structure resource.\n");
        }
    }
}

void VulkanTopLevelASKHR::build(VkCommandBuffer commandBuffer, BuildType buildType)
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
    case BuildType::BuildInitial:
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
        buildInfo.dstAccelerationStructure = accelerationStructure;
        break;
    case BuildType::UpdateInPlace:
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        buildInfo.srcAccelerationStructure = accelerationStructure;
        buildInfo.dstAccelerationStructure = accelerationStructure;
        break;
    }

    // TODO: Ensure we can safely use this scrash buffer! Maybe we need to use something like the UploadBuffer for this? As long as we only have a single TLAS this is fine though..
    // TODO: Ensure we can safely use this scrash buffer! Maybe we need to use something like the UploadBuffer for this? As long as we only have a single TLAS this is fine though..
    // TODO: Ensure we can safely use this scrash buffer! Maybe we need to use something like the UploadBuffer for this? As long as we only have a single TLAS this is fine though..
    // TODO: Ensure we can safely use this scrash buffer! Maybe we need to use something like the UploadBuffer for this? As long as we only have a single TLAS this is fine though..
    buildInfo.scratchData.deviceAddress = vulkanBackend.rayTracingKHR().sharedScratchBufferDeviceAddress();

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo { 0, 0, 0, 0 };
    rangeInfo.primitiveCount = instanceCount();

    VkAccelerationStructureBuildRangeInfoKHR* rangeInfosData = &rangeInfo;
    vulkanBackend.rayTracingKHR().vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &rangeInfosData);

    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanTopLevelASKHR::updateInstanceDataWithUploadBuffer(const std::vector<RTGeometryInstance>& newInstances, UploadBuffer& uploadBuffer)
{
    updateCurrentInstanceCount(static_cast<uint32_t>(newInstances.size()));
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    auto updatedInstanceData = createInstanceData(newInstances);
    uploadBuffer.upload(updatedInstanceData, *instanceBuffer);
}

std::vector<VkAccelerationStructureInstanceKHR> VulkanTopLevelASKHR::createInstanceData(const std::vector<RTGeometryInstance>& instances) const
{
    auto& vulkanBackend = static_cast<const VulkanBackend&>(backend());

    std::vector<VkAccelerationStructureInstanceKHR> instanceData {};
    instanceData.reserve(instances.size());

    for (size_t instanceIdx = 0; instanceIdx < instances.size(); ++instanceIdx) {
        const RTGeometryInstance& instance = instances[instanceIdx];

        auto* blas = dynamic_cast<const VulkanBottomLevelASKHR*>(&instance.blas);
        ASSERT(blas != nullptr); // ensure we do in face have a KHR version here

        VkAccelerationStructureInstanceKHR vkInstance {};
        vkInstance.transform = vulkanBackend.rayTracingKHR().toVkTransformMatrixKHR(instance.transform.worldMatrix());
        vkInstance.instanceCustomIndex = instanceIdx; // NOTE: This is gl_InstanceCustomIndexEXT, we should be smarter about this..
        vkInstance.accelerationStructureReference = blas->accelerationStructureDeviceAddress;
        vkInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        vkInstance.mask = 0xFF; //  Only be hit if rayMask & instance.mask != 0
        vkInstance.instanceShaderBindingTableRecordOffset = 0; // We will use the same hit group for all objects

        instanceData.push_back(vkInstance);
    }

    return instanceData;
}

VulkanBottomLevelASKHR::VulkanBottomLevelASKHR(Backend& backend, std::vector<RTGeometry> geos)
    : BottomLevelAS(backend, std::move(geos))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    ASSERT(vulkanBackend.hasRayTracingSupport());

    // All geometries in a BLAS must have the same type (i.e. AABB/triangles)
    bool isTriangleBLAS = geometries().front().hasTriangles();
    for (size_t i = 1; i < geometries().size(); ++i) {
        ASSERT(geometries()[i].hasTriangles() == isTriangleBLAS);
    }

    // TODO: Probably don't have a single buffer per transform. It's easy enough to manage a shared one for this.
    std::pair<VkBuffer, VmaAllocation> transformBufferAndAllocation;
    size_t singleTransformSize = 3 * 4 * sizeof(float);
    if (isTriangleBLAS) {
        std::vector<moos::mat3x4> transforms {};
        for (auto& geo : geometries()) {
            moos::mat3x4 mat34 = transpose(geo.triangles().transform);
            transforms.push_back(mat34);
        }

        size_t totalSize = transforms.size() * singleTransformSize;
        transformBufferAndAllocation = vulkanBackend.rayTracingKHR().createAccelerationStructureBuffer(totalSize, false, true); // TODO: Can this really be read-only?

        if (!vulkanBackend.setBufferMemoryUsingMapping(transformBufferAndAllocation.second, (uint8_t*)transforms.data(), totalSize)) {
            LogErrorAndExit("Error trying to copy data to the bottom level acceeration structure transform buffer.\n");
        }
    }

    std::vector<VkAccelerationStructureGeometryKHR> vkGeometries {};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> rangeInfos {};
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
                triangles.maxVertex = triGeo.vertexCount;
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
                VkBufferDeviceAddressInfo transformBufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
                transformBufferDeviceAddressInfo.buffer = transformBufferAndAllocation.first;
                VkDeviceAddress transformBufferBaseAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &transformBufferDeviceAddressInfo);

                triangles.transformData.deviceAddress = transformBufferBaseAddress; 
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

    VkAccelerationStructureBuildGeometryInfoKHR previewBuildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    previewBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    previewBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV; // (for now we're dealing with static geometry only so fast trace is ideal)
    previewBuildInfo.geometryCount = (uint32_t)vkGeometries.size();
    previewBuildInfo.pGeometries = vkGeometries.data();

    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vulkanBackend.rayTracingKHR().vkGetAccelerationStructureBuildSizesKHR(vulkanBackend.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &previewBuildInfo, maxPrimitiveCounts.data(), &buildSizesInfo);

    VkDeviceSize accelerationStructureBufferSize = buildSizesInfo.accelerationStructureSize; // (use min required size)
    auto accelerationStructureBufferAndAllocation = vulkanBackend.rayTracingKHR().createAccelerationStructureBuffer(accelerationStructureBufferSize, true, false);
    VkBuffer accelerationStructureBuffer = accelerationStructureBufferAndAllocation.first;

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    accelerationStructureCreateInfo.buffer = accelerationStructureBuffer;
    accelerationStructureCreateInfo.size = accelerationStructureBufferSize;
    accelerationStructureCreateInfo.offset = 0;

    if (vulkanBackend.rayTracingKHR().vkCreateAccelerationStructureKHR(vulkanBackend.device(), &accelerationStructureCreateInfo, nullptr, &accelerationStructure) != VK_SUCCESS) {
        LogErrorAndExit("Error trying to create bottom level acceleration structure\n");
    }

    VkAccelerationStructureDeviceAddressInfoKHR accelerationStructureDeviceAddressInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    accelerationStructureDeviceAddressInfo.accelerationStructure = accelerationStructure;
    accelerationStructureDeviceAddress = vulkanBackend.rayTracingKHR().vkGetAccelerationStructureDeviceAddressKHR(vulkanBackend.device(), &accelerationStructureDeviceAddressInfo);

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = previewBuildInfo;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = accelerationStructure;

    ASSERT(buildSizesInfo.buildScratchSize <= VulkanRayTracingKHR::SharedScratchBufferSize);
    buildInfo.scratchData.deviceAddress = vulkanBackend.rayTracingKHR().sharedScratchBufferDeviceAddress();

    VkAccelerationStructureBuildRangeInfoKHR* rangeInfosData = rangeInfos.data();
    bool buildSuccess = vulkanBackend.issueSingleTimeCommand([&](VkCommandBuffer cmdBuffer) {
        vulkanBackend.rayTracingKHR().vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildInfo, &rangeInfosData);
    });
    if (!buildSuccess) {
        LogErrorAndExit("Error trying to build bottom level acceleration structure\n");
    }

    associatedBuffers.push_back(accelerationStructureBufferAndAllocation);
    if (isTriangleBLAS) {
        associatedBuffers.push_back(transformBufferAndAllocation);
    }
}

VulkanBottomLevelASKHR::~VulkanBottomLevelASKHR()
{
    if (!hasBackend())
        return;

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vulkanBackend.rayTracingKHR().vkDestroyAccelerationStructureKHR(vulkanBackend.device(), accelerationStructure, nullptr);

    for (auto& [buffer, allocation] : associatedBuffers) {
        vmaDestroyBuffer(vulkanBackend.globalAllocator(), buffer, allocation);
    }
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
            LogWarning("Could not set debug name for vulkan bottom level acceleration structure resource.\n");
        }
    }
}