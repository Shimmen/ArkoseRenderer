#include "VulkanComputeState.h"

#include "backend/vulkan/VulkanBackend.h"
#include "backend/shader/ShaderManager.h"
#include "utility/Profiling.h"

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
