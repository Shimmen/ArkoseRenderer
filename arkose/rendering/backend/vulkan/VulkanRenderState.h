#pragma once

#include "rendering/backend/base/RenderState.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanRenderState final : public RenderState {
public:
    VulkanRenderState(Backend&, const RenderTarget&, VertexLayout, Shader, const StateBindings& stateBindings,
                      RasterState, DepthState, StencilState);
    virtual ~VulkanRenderState() override;

    virtual void setName(const std::string& name) override;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
};
