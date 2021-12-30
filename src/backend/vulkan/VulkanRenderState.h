#pragma once

#include "backend/base/RenderState.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanRenderState final : public RenderState {
public:
    VulkanRenderState(Backend&, const RenderTarget&, VertexLayout, Shader, const StateBindings& stateBindings,
                      Viewport, BlendState, RasterState, DepthState, StencilState);
    virtual ~VulkanRenderState() override;

    virtual void setName(const std::string& name) override;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
};
