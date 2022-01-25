#pragma once

#include "backend/base/BindingSet.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanBindingSet : public BindingSet {
public:
    VulkanBindingSet(Backend&, std::vector<ShaderBinding>);
    virtual ~VulkanBindingSet() override;

    virtual void setName(const std::string& name) override;

    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;
};
