#include "VulkanRayTracingStateKHR.h"

#include "rendering/backend/shader/ShaderManager.h"
#include "rendering/backend/vulkan/VulkanBackend.h"
#include "core/Logging.h"
#include "utility/Profiling.h"

VulkanRayTracingStateKHR::VulkanRayTracingStateKHR(Backend& backend, ShaderBindingTable sbt, const StateBindings& stateBindings, uint32_t maxRecursionDepth)
    : RayTracingState(backend, sbt, stateBindings, maxRecursionDepth)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);

    ARKOSE_ASSERT(vulkanBackend.hasRayTracingSupport());
    ARKOSE_ASSERT(vulkanBackend.rayTracingBackend() == VulkanBackend::RayTracingBackend::KhrExtension);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts {};
    for (const BindingSet* bindingSet : stateBindings.orderedBindingSets()) {
        auto* vulkanBindingSet = static_cast<const VulkanBindingSet*>(bindingSet);
        descriptorSetLayouts.push_back(vulkanBindingSet->descriptorSetLayout);
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
        ARKOSE_LOG(Fatal, "Error trying to create pipeline layout for ray tracing");
    }

    std::vector<VkShaderModule> shaderModulesToRemove {};
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages {};
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups {};

    // RayGen
    {
        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(sbt.rayGen());
        moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
        moduleCreateInfo.pCode = spirv.data();

        VkShaderModule shaderModule {};
        if (vkCreateShaderModule(vulkanBackend.device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            ARKOSE_LOG(Fatal, "Error trying to create shader module for raygen shader for ray tracing state");
        }

        VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageCreateInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        stageCreateInfo.module = shaderModule;
        stageCreateInfo.pName = "main";

        uint32_t shaderIndex = (uint32_t)shaderStages.size();
        shaderStages.push_back(stageCreateInfo);
        shaderModulesToRemove.push_back(shaderModule);

        VkRayTracingShaderGroupCreateInfoKHR shaderGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroup.generalShader = shaderIndex;

        shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

        shaderGroups.push_back(shaderGroup);
    }

    // HitGroups
    for (const HitGroup& hitGroup : sbt.hitGroups()) {

        VkRayTracingShaderGroupCreateInfoKHR shaderGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };

        shaderGroup.type = hitGroup.hasIntersectionShader()
            ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR
            : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;

        shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

        // ClosestHit
        {
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(hitGroup.closestHit());
            moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
            moduleCreateInfo.pCode = spirv.data();

            VkShaderModule shaderModule {};
            if (vkCreateShaderModule(vulkanBackend.device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                ARKOSE_LOG(Fatal, "Error trying to create shader module for closest hit shader for ray tracing state");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
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
                ARKOSE_LOG(Fatal, "Error trying to create shader module for anyhit shader for ray tracing state");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
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
                ARKOSE_LOG(Fatal, "Error trying to create shader module for intersection shader for ray tracing state");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
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
            ARKOSE_LOG(Fatal, "Error trying to create shader module for miss shader for ray tracing state");
        }

        VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageCreateInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        stageCreateInfo.module = shaderModule;
        stageCreateInfo.pName = "main";

        uint32_t shaderIndex = (uint32_t)shaderStages.size();
        shaderStages.push_back(stageCreateInfo);
        shaderModulesToRemove.push_back(shaderModule);

        VkRayTracingShaderGroupCreateInfoKHR shaderGroup = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroup.generalShader = shaderIndex;

        shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

        shaderGroups.push_back(shaderGroup);
    }

    VkRayTracingPipelineCreateInfoKHR rtPipelineCreateInfo { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };

    rtPipelineCreateInfo.flags = 0u;
    rtPipelineCreateInfo.maxPipelineRayRecursionDepth = maxRecursionDepth;
    rtPipelineCreateInfo.stageCount = (uint32_t)shaderStages.size();
    rtPipelineCreateInfo.pStages = shaderStages.data();
    rtPipelineCreateInfo.groupCount = (uint32_t)shaderGroups.size();
    rtPipelineCreateInfo.pGroups = shaderGroups.data();
    rtPipelineCreateInfo.layout = pipelineLayout;

    // This is not a library pipeline state
    rtPipelineCreateInfo.pLibraryInfo = nullptr;
    rtPipelineCreateInfo.pLibraryInterface = nullptr;
    rtPipelineCreateInfo.pDynamicState = nullptr;

    // Not deriving from another pipeline
    rtPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    rtPipelineCreateInfo.basePipelineIndex = -1;

    VkDeferredOperationKHR deferredOperation = VK_NULL_HANDLE;
    if (vulkanBackend.rayTracingKHR().vkCreateRayTracingPipelinesKHR(vulkanBackend.device(), deferredOperation, vulkanBackend.pipelineCache(), 1, &rtPipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error creating ray tracing pipeline");
    }

    // Remove shader modules after creating the pipeline
    for (VkShaderModule& shaderModule : shaderModulesToRemove) {
        vkDestroyShaderModule(vulkanBackend.device(), shaderModule, nullptr);
    }

    // Create buffer for the shader binding table
    {
        uint32_t sizeOfSingleHandle = vulkanBackend.rayTracingKHR().pipelineStateProperties().shaderGroupHandleSize;
        uint32_t sizeOfAllHandles = sizeOfSingleHandle * (uint32_t)shaderGroups.size();
        std::vector<std::byte> shaderGroupHandles { sizeOfAllHandles };
        if (vulkanBackend.rayTracingKHR().vkGetRayTracingShaderGroupHandlesKHR(vulkanBackend.device(), pipeline, 0, (uint32_t)shaderGroups.size(), sizeOfAllHandles, shaderGroupHandles.data()) != VK_SUCCESS) {
            ARKOSE_LOG(Fatal, "Error trying to get shader group handles for the shader binding table.");
        }

        // TODO: For now we don't have any data, only shader handles, but we still have to consider the alignments & strides
        uint32_t baseAlignment = vulkanBackend.rayTracingKHR().pipelineStateProperties().shaderGroupBaseAlignment;
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
        sbtBufferCreateInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT;
        sbtBufferCreateInfo.size = sbtSize;

        if (vulkanDebugMode) {
            // for nsight debugging & similar stuff)
            sbtBufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }

        VmaAllocationCreateInfo sbtAllocCreateInfo = {};
        sbtAllocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU; // Gpu only is probably perfectly fine, except we need to copy the data using a staging buffer

        if (vmaCreateBuffer(vulkanBackend.globalAllocator(), &sbtBufferCreateInfo, &sbtAllocCreateInfo, &sbtBuffer, &sbtBufferAllocation, nullptr) != VK_SUCCESS) {
            ARKOSE_LOG(Fatal, "Error trying to create buffer for the shader binding table.");
        }

        if (!vulkanBackend.setBufferMemoryUsingMapping(sbtBufferAllocation, (uint8_t*)sbtData.data(), sbtSize)) {
            ARKOSE_LOG(Fatal, "Error trying to copy data to the shader binding table.");
        }
    }
}

VulkanRayTracingStateKHR::~VulkanRayTracingStateKHR()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vmaDestroyBuffer(vulkanBackend.globalAllocator(), sbtBuffer, sbtBufferAllocation);
    vkDestroyPipeline(vulkanBackend.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(vulkanBackend.device(), pipelineLayout, nullptr);
}

void VulkanRayTracingStateKHR::setName(const std::string& name)
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
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan ray tracing pipeline resource.");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipelineLayout);
            nameInfo.pObjectName = pipelineLayoutName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan ray tracing pipeline layout resource.");
            }
        }
    }
}

void VulkanRayTracingStateKHR::traceRaysWithShaderOnlySBT(VkCommandBuffer commandBuffer, Extent2D extent) const
{
    SCOPED_PROFILE_ZONE_BACKEND();

    auto& vulkanBackend = static_cast<const VulkanBackend&>(backend());

    // NOTE: Right now all shader groups work out to exactly one of these alignments in size
    uint32_t shaderGroupBaseAlignment = vulkanBackend.rayTracingKHR().pipelineStateProperties().shaderGroupBaseAlignment;

    VkBufferDeviceAddressInfo sbtBufferDeviceAddressInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    sbtBufferDeviceAddressInfo.buffer = sbtBuffer;
    VkDeviceAddress sbtBufferBaseAddress = vkGetBufferDeviceAddress(vulkanBackend.device(), &sbtBufferDeviceAddressInfo);

    uint32_t numHitGroups = (uint32_t)shaderBindingTable().hitGroups().size();
    uint32_t numMissShaders = (uint32_t)shaderBindingTable().missShaders().size();

    VkStridedDeviceAddressRegionKHR raygenSbtRegion;
    raygenSbtRegion.deviceAddress = sbtBufferBaseAddress;
    raygenSbtRegion.stride = shaderGroupBaseAlignment;
    raygenSbtRegion.size = raygenSbtRegion.stride;

    VkStridedDeviceAddressRegionKHR hitSbtRegion;
    hitSbtRegion.deviceAddress = raygenSbtRegion.deviceAddress + raygenSbtRegion.size;
    hitSbtRegion.stride = shaderGroupBaseAlignment;
    hitSbtRegion.size = numHitGroups * hitSbtRegion.stride;

    VkStridedDeviceAddressRegionKHR missSbtRegion;
    missSbtRegion.deviceAddress = hitSbtRegion.deviceAddress + hitSbtRegion.size;
    missSbtRegion.stride = shaderGroupBaseAlignment;
    missSbtRegion.size = numMissShaders * missSbtRegion.stride;

    // NOTE: No support for callable shaders yet
    VkStridedDeviceAddressRegionKHR callableSbtRegion { 0, 0, 0 };

    vulkanBackend.rayTracingKHR().vkCmdTraceRaysKHR(commandBuffer,
                                                    &raygenSbtRegion, &missSbtRegion, &hitSbtRegion, &callableSbtRegion,
                                                    extent.width(), extent.height(), 1);
}
