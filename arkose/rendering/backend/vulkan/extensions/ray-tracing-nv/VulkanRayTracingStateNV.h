#pragma once

#include "rendering/backend/base/RayTracingState.h"

#include "utility/Extent.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanRayTracingStateNV final : public RayTracingState {
public:
    VulkanRayTracingStateNV(Backend&, ShaderBindingTable, const StateBindings&, uint32_t maxRecursionDepth);
    virtual ~VulkanRayTracingStateNV() override;

    virtual void setName(const std::string& name) override;

    void traceRays(VkCommandBuffer, Extent2D) const;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    VkBuffer sbtBuffer;
    VmaAllocation sbtBufferAllocation;
};
