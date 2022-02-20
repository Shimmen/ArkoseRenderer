#pragma once

#include "backend/base/RayTracingState.h"

#include "utility/Extent.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanRayTracingStateKHR final : public RayTracingState {
public:
    VulkanRayTracingStateKHR(Backend&, ShaderBindingTable, const StateBindings&, uint32_t maxRecursionDepth);
    virtual ~VulkanRayTracingStateKHR() override;

    virtual void setName(const std::string& name) override;

    void traceRaysWithShaderOnlySBT(VkCommandBuffer, Extent2D) const;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    VkBuffer sbtBuffer;
    VmaAllocation sbtBufferAllocation;
};
