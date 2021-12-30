#pragma once

#include "backend/base/RayTracingState.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanRayTracingState final : public RayTracingState {
public:
    VulkanRayTracingState(Backend&, ShaderBindingTable, const StateBindings&, uint32_t maxRecursionDepth);
    virtual ~VulkanRayTracingState() override;

    virtual void setName(const std::string& name) override;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    VkBuffer sbtBuffer;
    VmaAllocation sbtBufferAllocation;
};
