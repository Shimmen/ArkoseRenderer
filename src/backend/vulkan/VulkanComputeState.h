#pragma once

#include "backend/base/ComputeState.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanComputeState final : public ComputeState {
public:
    VulkanComputeState(Backend&, Shader, std::vector<BindingSet*>);
    virtual ~VulkanComputeState() override;

    virtual void setName(const std::string& name) override;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    std::vector<Texture*> sampledTextures;
    std::vector<Texture*> storageImages;
};