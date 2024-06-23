#include "VulkanComputeState.h"

#include "rendering/backend/vulkan/VulkanBackend.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "core/Logging.h"
#include "utility/Profiling.h"

VulkanComputeState::VulkanComputeState(Backend& backend, Shader shader, StateBindings const& stateBindings)
    : ComputeState(backend, shader, stateBindings)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend);

    VkPipelineShaderStageCreateInfo computeShaderStage { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    computeShaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStage.pName = "main";
    {
        ARKOSE_ASSERT(shader.type() == ShaderType::Compute);
        ARKOSE_ASSERT(shader.files().size() == 1);

        ShaderFile const& file = shader.files().front();
        ARKOSE_ASSERT(file.shaderStage() == ShaderStage::Compute);

        // TODO: Maybe don't create new modules every time? Currently they are deleted later in this function
        VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(file);
        moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
        moduleCreateInfo.pCode = spirv.data();

        VkShaderModule shaderModule {};
        if (vkCreateShaderModule(vulkanBackend.device(), &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            ARKOSE_LOG(Fatal, "Error trying to create shader module");
        }

        computeShaderStage.module = shaderModule;
    }

    m_namedConstantLookup = ShaderManager::instance().mergeNamedConstants(shader);

    //
    // Create pipeline layout
    //

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts {};
    for (BindingSet const* bindingSet : stateBindings.orderedBindingSets()) {
        if (auto* vulkanBindingSet = static_cast<const VulkanBindingSet*>(bindingSet)) {
            descriptorSetLayouts.push_back(vulkanBindingSet->descriptorSetLayout);
        } else {
            descriptorSetLayouts.push_back(vulkanBackend.emptyDescriptorSetLayout());
        }
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
        ARKOSE_LOG(Fatal, "Error trying to create pipeline layout");
    }

    //
    // Create pipeline
    //

    VkComputePipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };

    pipelineCreateInfo.stage = computeShaderStage;
    pipelineCreateInfo.layout = pipelineLayout;
    pipelineCreateInfo.flags = 0u;

    if (vkCreateComputePipelines(vulkanBackend.device(), vulkanBackend.pipelineCache(), 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create compute pipeline");
    }

    // Remove shader modules, they are no longer needed after creating the pipeline
    vkDestroyShaderModule(vulkanBackend.device(), computeShaderStage.module, nullptr);

    for (BindingSet const* bindingSet : stateBindings.orderedBindingSets()) {
        for (auto& bindingInfo : bindingSet->shaderBindings()) {
            if (bindingInfo.type() == ShaderBindingType::SampledTexture) {
                for (auto& texture : bindingInfo.getSampledTextures()) {
                    sampledTextures.push_back(texture);
                }
            } else if (bindingInfo.type() == ShaderBindingType::StorageTexture) {
                for (const TextureMipView& textureMip : bindingInfo.getStorageTextures()) {
                    storageImages.push_back(textureMip);
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
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan compute pipeline resource.");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipelineLayout);
            nameInfo.pObjectName = pipelineLayoutName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan compute pipeline layout resource.");
            }
        }
    }
}
