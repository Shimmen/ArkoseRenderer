#pragma once

#include "rendering/backend/base/RenderState.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanRenderState final : public RenderState {
public:
    VulkanRenderState(Backend&, RenderTarget const&, std::vector<VertexLayout> const&, Shader, StateBindings const&,
                      RasterState, DepthState, StencilState);
    virtual ~VulkanRenderState() override;

    virtual void setName(const std::string& name) override;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
};
