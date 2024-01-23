#pragma once

#include "rendering/backend/base/ComputeState.h"

#include "rendering/backend/base/Texture.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanComputeState final : public ComputeState {
public:
    VulkanComputeState(Backend&, Shader, StateBindings const&);
    virtual ~VulkanComputeState() override;

    virtual void setName(const std::string& name) override;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    std::vector<Texture const*> sampledTextures;
    std::vector<TextureMipView> storageImages;
};
