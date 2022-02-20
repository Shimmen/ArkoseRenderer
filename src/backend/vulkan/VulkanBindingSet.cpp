#include "VulkanBindingSet.h"

#include "backend/vulkan/VulkanBackend.h"
#include "utility/CapList.h"
#include "utility/Profiling.h"

VulkanBindingSet::VulkanBindingSet(Backend& backend, std::vector<ShaderBinding> bindings)
    : BindingSet(backend, std::move(bindings))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& vulkanBackend = static_cast<const VulkanBackend&>(backend);
    const auto& device = vulkanBackend.device();

    // Create descriptor pool
    {
        // TODO: Maybe in the future we don't want one pool per shader binding state? We could group a lot of stuff together probably..?

        std::unordered_map<ShaderBindingType, size_t> bindingTypeIndex {};
        std::vector<VkDescriptorPoolSize> descriptorPoolSizes {};

        for (auto& bindingInfo : shaderBindings()) {

            ShaderBindingType type = bindingInfo.type;

            auto entry = bindingTypeIndex.find(type);
            if (entry == bindingTypeIndex.end()) {

                VkDescriptorPoolSize poolSize = {};
                poolSize.descriptorCount = bindingInfo.count;

                switch (bindingInfo.type) {
                case ShaderBindingType::UniformBuffer:
                    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    break;
                case ShaderBindingType::StorageBuffer:
                case ShaderBindingType::StorageBufferArray:
                    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    break;
                case ShaderBindingType::StorageImage:
                    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    break;
                case ShaderBindingType::TextureSampler:
                case ShaderBindingType::TextureSamplerArray:
                    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    break;
                case ShaderBindingType::RTAccelerationStructure:
                    switch (vulkanBackend.rayTracingBackend()) {
                    case VulkanBackend::RayTracingBackend::RtxExtension:
                        poolSize.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
                        break;
                    case VulkanBackend::RayTracingBackend::KhrExtension:
                        poolSize.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                        break;
                    }
                    break;
                default:
                    ASSERT_NOT_REACHED();
                }

                bindingTypeIndex[type] = descriptorPoolSizes.size();
                descriptorPoolSizes.push_back(poolSize);

            } else {

                size_t index = entry->second;
                VkDescriptorPoolSize& poolSize = descriptorPoolSizes[index];
                poolSize.descriptorCount += bindingInfo.count;
            }
        }

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        descriptorPoolCreateInfo.poolSizeCount = (uint32_t)descriptorPoolSizes.size();
        descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
        descriptorPoolCreateInfo.maxSets = 1;

        if (vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor pool\n");
        }
    }

    // Create descriptor set layout
    {
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings {};
        layoutBindings.reserve(shaderBindings().size());

        for (auto& bindingInfo : shaderBindings()) {

            VkDescriptorSetLayoutBinding binding = {};
            binding.binding = bindingInfo.bindingIndex;
            binding.descriptorCount = bindingInfo.count;

            switch (bindingInfo.type) {
            case ShaderBindingType::UniformBuffer:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case ShaderBindingType::StorageBuffer:
            case ShaderBindingType::StorageBufferArray:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case ShaderBindingType::StorageImage:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;
            case ShaderBindingType::TextureSampler:
            case ShaderBindingType::TextureSamplerArray:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                break;
            case ShaderBindingType::RTAccelerationStructure:
                switch (vulkanBackend.rayTracingBackend()) {
                case VulkanBackend::RayTracingBackend::RtxExtension:
                    binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
                    break;
                case VulkanBackend::RayTracingBackend::KhrExtension:
                    binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                    break;
                }
                break;
            default:
                ASSERT_NOT_REACHED();
            }

            const auto& vulkanBackend = static_cast<const VulkanBackend&>(backend);
            binding.stageFlags = vulkanBackend.shaderStageToVulkanShaderStageFlags(bindingInfo.shaderStage);

            binding.pImmutableSamplers = nullptr;

            layoutBindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptorSetLayoutCreateInfo.bindingCount = (uint32_t)layoutBindings.size();
        descriptorSetLayoutCreateInfo.pBindings = layoutBindings.data();

        //VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
        //bindingFlagsCreateInfo. ...

        //descriptorSetLayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        //descriptorSetLayoutCreateInfo.pNext = &bindingFlagsCreateInfo;

        if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set layout\n");
        }
    }

    // Create descriptor set
    {
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet) != VK_SUCCESS) {
            LogErrorAndExit("Error trying to create descriptor set\n");
        }
    }

    // Update descriptor set
    {
        std::vector<VkWriteDescriptorSet> descriptorSetWrites {};
        CapList<VkDescriptorBufferInfo> descBufferInfos { 1024 };
        CapList<VkDescriptorImageInfo> descImageInfos { 1024 };
        CapList<VkWriteDescriptorSetAccelerationStructureNV> rtxAccelStructWrites { 10 };
        CapList<VkWriteDescriptorSetAccelerationStructureKHR> khrAccelStructWrites { 10 };

        for (auto& bindingInfo : shaderBindings()) {

            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.pTexelBufferView = nullptr;

            write.dstSet = descriptorSet;
            write.dstBinding = bindingInfo.bindingIndex;

            switch (bindingInfo.type) {
            case ShaderBindingType::UniformBuffer: {

                ASSERT(bindingInfo.buffers.size() == 1);
                ASSERT(bindingInfo.buffers[0]);
                auto& buffer = static_cast<const VulkanBuffer&>(*bindingInfo.buffers[0]);

                VkDescriptorBufferInfo descBufferInfo {};
                descBufferInfo.offset = 0;
                descBufferInfo.range = VK_WHOLE_SIZE;
                descBufferInfo.buffer = buffer.buffer;

                descBufferInfos.push_back(descBufferInfo);
                write.pBufferInfo = &descBufferInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::StorageBuffer: {

                ASSERT(bindingInfo.buffers.size() == 1);
                ASSERT(bindingInfo.buffers[0]);
                auto& buffer = static_cast<const VulkanBuffer&>(*bindingInfo.buffers[0]);

                VkDescriptorBufferInfo descBufferInfo {};
                descBufferInfo.offset = 0;
                descBufferInfo.range = VK_WHOLE_SIZE;
                descBufferInfo.buffer = buffer.buffer;

                descBufferInfos.push_back(descBufferInfo);
                write.pBufferInfo = &descBufferInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::StorageBufferArray: {

                ASSERT(bindingInfo.count == bindingInfo.buffers.size());

                if (bindingInfo.count == 0) {
                    continue;
                }

                for (const Buffer* buffer : bindingInfo.buffers) {

                    ASSERT(buffer);
                    ASSERT(buffer->usage() == Buffer::Usage::StorageBuffer);
                    auto& vulkanBuffer = static_cast<const VulkanBuffer&>(*buffer);

                    VkDescriptorBufferInfo descBufferInfo {};
                    descBufferInfo.offset = 0;
                    descBufferInfo.range = VK_WHOLE_SIZE;
                    descBufferInfo.buffer = vulkanBuffer.buffer;

                    descBufferInfos.push_back(descBufferInfo);
                }

                // NOTE: This should point at the first VkDescriptorBufferInfo
                write.pBufferInfo = &descBufferInfos.back() - (bindingInfo.count - 1);
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write.descriptorCount = bindingInfo.count;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::StorageImage: {

                ASSERT(bindingInfo.textures.size() == 1);
                ASSERT(bindingInfo.textures[0]);
                auto& texture = static_cast<const VulkanTexture&>(*bindingInfo.textures[0]);

                VkDescriptorImageInfo descImageInfo {};
                descImageInfo.sampler = texture.sampler;
                descImageInfo.imageView = texture.imageView;

                // The runtime systems make sure that the input texture is in the layout!
                descImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                descImageInfos.push_back(descImageInfo);
                write.pImageInfo = &descImageInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::TextureSampler: {

                ASSERT(bindingInfo.textures.size() == 1);
                ASSERT(bindingInfo.textures[0]);
                auto& texture = static_cast<const VulkanTexture&>(*bindingInfo.textures[0]);

                VkDescriptorImageInfo descImageInfo {};
                descImageInfo.sampler = texture.sampler;
                descImageInfo.imageView = texture.imageView;

                // The runtime systems make sure that the input texture is in the layout!
                descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                descImageInfos.push_back(descImageInfo);
                write.pImageInfo = &descImageInfos.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::TextureSamplerArray: {

                size_t numTextures = bindingInfo.textures.size();
                ASSERT(numTextures > 0);

                for (uint32_t i = 0; i < bindingInfo.count; ++i) {

                    // NOTE: We always have to fill in the count here, but for the unused we just fill with a "default"
                    const Texture* genTexture = (i >= numTextures) ? bindingInfo.textures.front() : bindingInfo.textures[i];
                    ASSERT(genTexture);

                    auto& texture = static_cast<const VulkanTexture&>(*genTexture);

                    VkDescriptorImageInfo descImageInfo {};
                    descImageInfo.sampler = texture.sampler;
                    descImageInfo.imageView = texture.imageView;

                    // The runtime systems make sure that the input texture is in the layout!
                    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    descImageInfos.push_back(descImageInfo);
                }

                // NOTE: This should point at the first VkDescriptorImageInfo
                write.pImageInfo = &descImageInfos.back() - (bindingInfo.count - 1);
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = bindingInfo.count;
                write.dstArrayElement = 0;

                break;
            }

            case ShaderBindingType::RTAccelerationStructure: {

                ASSERT(bindingInfo.textures.empty());
                ASSERT(bindingInfo.buffers.empty());
                ASSERT(bindingInfo.tlas != nullptr);

                switch (vulkanBackend.rayTracingBackend()) {
                case VulkanBackend::RayTracingBackend::RtxExtension: {

                    VkWriteDescriptorSetAccelerationStructureNV descriptorAccelerationStructureInfo { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV };
                    descriptorAccelerationStructureInfo.pAccelerationStructures = &static_cast<const VulkanTopLevelAS&>(*bindingInfo.tlas).accelerationStructure;
                    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;

                    rtxAccelStructWrites.push_back(descriptorAccelerationStructureInfo);
                    write.pNext = &rtxAccelStructWrites.back();
                    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;

                } break;
                
                case VulkanBackend::RayTracingBackend::KhrExtension: {

                    VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
                    descriptorAccelerationStructureInfo.pAccelerationStructures = &static_cast<const VulkanTopLevelASKHR&>(*bindingInfo.tlas).accelerationStructure;
                    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;

                    khrAccelStructWrites.push_back(descriptorAccelerationStructureInfo);
                    write.pNext = &khrAccelStructWrites.back();
                    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

                } break;
                }

                write.descriptorCount = 1;
                write.dstArrayElement = 0;

                break;
            }

            default:
                ASSERT_NOT_REACHED();
            }

            descriptorSetWrites.push_back(write);
        }

        vkUpdateDescriptorSets(device, (uint32_t)descriptorSetWrites.size(), descriptorSetWrites.data(), 0, nullptr);
    }
}

VulkanBindingSet::~VulkanBindingSet()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vkDestroyDescriptorPool(vulkanBackend.device(), descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(vulkanBackend.device(), descriptorSetLayout, nullptr);
}

void VulkanBindingSet::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        std::string descriptorSetName = name + "-descriptorSet";
        std::string descriptorPoolName = name + "-descriptorPool";
        std::string descriptorSetLayoutName = name + "-descriptorSetLayout";

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(descriptorSet);
            nameInfo.pObjectName = descriptorSetName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan descriptor set resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_DESCRIPTOR_POOL;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(descriptorPool);
            nameInfo.pObjectName = descriptorPoolName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan descriptor pool resource.\n");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(descriptorSetLayout);
            nameInfo.pObjectName = descriptorSetLayoutName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                LogWarning("Could not set debug name for vulkan descriptor set layout resource.\n");
            }
        }
    }
}

