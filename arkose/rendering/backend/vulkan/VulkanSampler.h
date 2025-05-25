#pragma once

#include "rendering/backend/base/Sampler.h"

#include <vulkan/vulkan.h>

class Backend;

struct VulkanSampler final : public Sampler {
public:
    VulkanSampler() = default;
    VulkanSampler(Backend&, Description&);
    virtual ~VulkanSampler() override;

    virtual void setName(std::string const& name) override;

    VkSampler sampler { VK_NULL_HANDLE };
};
