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

            ShaderBindingType type = bindingInfo.type();

            auto entry = bindingTypeIndex.find(type);
            if (entry == bindingTypeIndex.end()) {

                VkDescriptorPoolSize poolSize = {};
                poolSize.descriptorCount = bindingInfo.arrayCount();

                switch (type) {
                case ShaderBindingType::ConstantBuffer:
                    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    break;
                case ShaderBindingType::StorageBuffer:
                    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    break;
                case ShaderBindingType::StorageTexture:
                    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    break;
                case ShaderBindingType::SampledTexture:
                    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    break;
                case ShaderBindingType::RTAccelerationStructure:
                    switch (vulkanBackend.rayTracingBackend()) {
                    case VulkanBackend::RayTracingBackend::NvExtension:
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
                poolSize.descriptorCount += bindingInfo.arrayCount();
            }
        }

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        descriptorPoolCreateInfo.poolSizeCount = (uint32_t)descriptorPoolSizes.size();
        descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
        descriptorPoolCreateInfo.maxSets = 1;

        if (vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            ARKOSE_LOG(Fatal, "Error trying to create descriptor pool");
        }
    }

    // Create descriptor set layout
    {
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings {};
        layoutBindings.reserve(shaderBindings().size());

        std::vector<VkDescriptorBindingFlags> bindingFlags {};
        bindingFlags.reserve(shaderBindings().size());

        for (auto& bindingInfo : shaderBindings()) {

            VkDescriptorSetLayoutBinding binding = {};
            binding.binding = bindingInfo.bindingIndex();
            binding.descriptorCount = bindingInfo.arrayCount();

            VkDescriptorBindingFlags flagsForBinding = 0u;

            switch (bindingInfo.type()) {
            case ShaderBindingType::ConstantBuffer:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case ShaderBindingType::StorageBuffer:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case ShaderBindingType::StorageTexture:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;
            case ShaderBindingType::SampledTexture:
                binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                flagsForBinding |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT; // TODO: Maybe allow this for more/all types?
                break;
            case ShaderBindingType::RTAccelerationStructure:
                switch (vulkanBackend.rayTracingBackend()) {
                case VulkanBackend::RayTracingBackend::NvExtension:
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
            binding.stageFlags = vulkanBackend.shaderStageToVulkanShaderStageFlags(bindingInfo.shaderStage());

            binding.pImmutableSamplers = nullptr;

            layoutBindings.push_back(binding);
            bindingFlags.push_back(flagsForBinding);
        }

        ARKOSE_ASSERT(bindingFlags.size() == layoutBindings.size());

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptorSetLayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
        descriptorSetLayoutCreateInfo.pBindings = layoutBindings.data();

        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
        bindingFlagsCreateInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        bindingFlagsCreateInfo.pBindingFlags = bindingFlags.data();

        descriptorSetLayoutCreateInfo.pNext = &bindingFlagsCreateInfo;

        if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            ARKOSE_LOG(Fatal, "Error trying to create descriptor set layout");
        }
    }

    // Create descriptor set
    {
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet) != VK_SUCCESS) {
            ARKOSE_LOG(Fatal, "Error trying to create descriptor set");
        }
    }

    updateBindings();
}

VulkanBindingSet::~VulkanBindingSet()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());

    for (VkImageView imageView : m_additionalImageViews) {
        vkDestroyImageView(vulkanBackend.device(), imageView, nullptr);
    }

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
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan descriptor set resource.");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_DESCRIPTOR_POOL;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(descriptorPool);
            nameInfo.pObjectName = descriptorPoolName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan descriptor pool resource.");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(descriptorSetLayout);
            nameInfo.pObjectName = descriptorSetLayoutName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan descriptor set layout resource.");
            }
        }
    }
}

void VulkanBindingSet::updateBindings()
{
    auto& vulkanBackend = static_cast<const VulkanBackend&>(backend());

    std::vector<VkWriteDescriptorSet> descriptorSetWrites {};
    CapList<VkDescriptorBufferInfo> descBufferInfos { 4096 };
    CapList<VkDescriptorImageInfo> descImageInfos { 4096 };
    CapList<VkWriteDescriptorSetAccelerationStructureNV> rtxAccelStructWrites { 10 };
    CapList<VkWriteDescriptorSetAccelerationStructureKHR> khrAccelStructWrites { 10 };

    for (auto& bindingInfo : shaderBindings()) {

        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.pTexelBufferView = nullptr;

        write.dstSet = descriptorSet;
        write.dstBinding = bindingInfo.bindingIndex();

        switch (bindingInfo.type()) {
        case ShaderBindingType::ConstantBuffer: {

            auto& buffer = static_cast<const VulkanBuffer&>(bindingInfo.buffer());

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

            ARKOSE_ASSERT(bindingInfo.arrayCount() == bindingInfo.buffers().size());

            if (bindingInfo.arrayCount() == 0) {
                continue;
            }

            for (const Buffer* buffer : bindingInfo.buffers()) {

                ARKOSE_ASSERT(buffer);
                auto& vulkanBuffer = static_cast<const VulkanBuffer&>(*buffer);

                VkDescriptorBufferInfo descBufferInfo {};
                descBufferInfo.offset = 0;
                descBufferInfo.range = VK_WHOLE_SIZE;
                descBufferInfo.buffer = vulkanBuffer.buffer;

                descBufferInfos.push_back(descBufferInfo);
            }

            // NOTE: This should point at the first VkDescriptorBufferInfo of the ones just pushed
            write.pBufferInfo = &descBufferInfos.back() - (bindingInfo.arrayCount() - 1);
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.descriptorCount = bindingInfo.arrayCount();
            write.dstArrayElement = 0;

            break;
        }

        case ShaderBindingType::StorageTexture: {

            auto& texture = static_cast<const VulkanTexture&>(bindingInfo.storageTexture().texture());
            uint32_t mipLevel = bindingInfo.storageTexture().mipLevel();

            VkDescriptorImageInfo descImageInfo {};
            if (mipLevel == 0) {
                // All textures have an image view for mip0 already available
                descImageInfo.imageView = texture.imageView;
            } else {
                descImageInfo.imageView = texture.createImageView(mipLevel, 1);
                m_additionalImageViews.push_back(descImageInfo.imageView);
            }

            // The runtime systems make sure that the input texture is in the layout!
            descImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            descImageInfos.push_back(descImageInfo);
            write.pImageInfo = &descImageInfos.back();
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

            write.descriptorCount = 1;
            write.dstArrayElement = 0;

            break;
        }

        case ShaderBindingType::SampledTexture: {

            const auto& sampledTextures = bindingInfo.sampledTextures();
            size_t numTextures = sampledTextures.size();
            ARKOSE_ASSERT(numTextures > 0);

            for (uint32_t i = 0; i < bindingInfo.arrayCount(); ++i) {

                // NOTE: We always have to fill in the count here, but for the unused we just fill with a "default"
                const Texture* genTexture = (i >= numTextures) ? sampledTextures.front() : sampledTextures[i];
                ARKOSE_ASSERT(genTexture);

                auto& texture = static_cast<const VulkanTexture&>(*genTexture);

                VkDescriptorImageInfo descImageInfo {};
                descImageInfo.sampler = texture.sampler;
                descImageInfo.imageView = texture.imageView;

                // The runtime systems make sure that the input texture is in the layout!
                descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                descImageInfos.push_back(descImageInfo);
            }

            // NOTE: This should point at the first VkDescriptorImageInfo of the ones we just pushed
            write.pImageInfo = &descImageInfos.back() - (bindingInfo.arrayCount() - 1);
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = bindingInfo.arrayCount();
            write.dstArrayElement = 0;

            break;
        }

        case ShaderBindingType::RTAccelerationStructure: {

            switch (vulkanBackend.rayTracingBackend()) {
            case VulkanBackend::RayTracingBackend::NvExtension: {

                VkWriteDescriptorSetAccelerationStructureNV descriptorAccelerationStructureInfo { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV };
                descriptorAccelerationStructureInfo.pAccelerationStructures = &static_cast<const VulkanTopLevelASNV&>(bindingInfo.topLevelAS()).accelerationStructure;
                descriptorAccelerationStructureInfo.accelerationStructureCount = 1;

                rtxAccelStructWrites.push_back(descriptorAccelerationStructureInfo);
                write.pNext = &rtxAccelStructWrites.back();
                write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;

            } break;

            case VulkanBackend::RayTracingBackend::KhrExtension: {

                VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
                descriptorAccelerationStructureInfo.pAccelerationStructures = &static_cast<const VulkanTopLevelASKHR&>(bindingInfo.topLevelAS()).accelerationStructure;
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

    // TODO: We might want to batch multiple writes. This function is clearly not made for updating a single descriptor set.
    vkUpdateDescriptorSets(vulkanBackend.device(), static_cast<uint32_t>(descriptorSetWrites.size()), descriptorSetWrites.data(), 0, nullptr);
}

void VulkanBindingSet::updateTextures(uint32_t bindingIndex, const std::vector<TextureBindingUpdate>& textureUpdates)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    if (bindingIndex >= shaderBindings().size()) {
        ARKOSE_LOG(Fatal, "BindingSet: trying to update texture for out-of-bounds shader binding, exiting.");
    }

    ShaderBindingType bindingType = shaderBindings()[bindingIndex].type();
    if (bindingType != ShaderBindingType::SampledTexture) {
        ARKOSE_LOG(Fatal, "BindingSet: trying to update texture for shader binding that does not have texture(s), exiting.");
    }

    std::vector<VkWriteDescriptorSet> descriptorSetWrites {};
    std::vector<VkDescriptorImageInfo> descImageInfos {};

    descriptorSetWrites.reserve(textureUpdates.size());
    descImageInfos.reserve(textureUpdates.size());

    for (const TextureBindingUpdate& textureUpdate : textureUpdates) {

        ARKOSE_ASSERT(textureUpdate.texture != nullptr);
        VulkanTexture& texture = *static_cast<VulkanTexture*>(textureUpdate.texture);

        VkDescriptorImageInfo descImageInfo {};
        descImageInfo.sampler = texture.sampler;
        descImageInfo.imageView = texture.imageView;
        descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descImageInfos.push_back(descImageInfo);

        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = descriptorSet;

        write.dstBinding = bindingIndex;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

        // TODO: Would it be a good idea to batch multiple together if they are consecutive?
        write.dstArrayElement = textureUpdate.index;
        write.descriptorCount = 1;
        write.pImageInfo = &descImageInfos.back();

        write.pBufferInfo = nullptr;
        write.pTexelBufferView = nullptr;

        descriptorSetWrites.push_back(write);
    }

    auto& vulkanBackend = static_cast<const VulkanBackend&>(backend());
    vkUpdateDescriptorSets(vulkanBackend.device(), static_cast<uint32_t>(descriptorSetWrites.size()), descriptorSetWrites.data(), 0, nullptr);
}
