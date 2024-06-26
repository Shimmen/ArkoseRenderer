#pragma once

#include "rendering/backend/base/BindingSet.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanBindingSet : public BindingSet {
public:
    VulkanBindingSet(Backend&, std::vector<ShaderBinding>);
    virtual ~VulkanBindingSet() override;

    virtual void setName(const std::string& name) override;

    void updateBindings();

    virtual void updateTextures(uint32_t index, const std::vector<TextureBindingUpdate>&) override;

    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;

private:
    std::vector<VkImageView> m_additionalImageViews {};
};
