#include "VulkanRenderState.h"

#include "rendering/backend/vulkan/VulkanBackend.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "core/Logging.h"
#include "utility/Profiling.h"

VulkanRenderState::VulkanRenderState(Backend& backend, RenderTarget const& renderTarget, std::vector<VertexLayout> const& vertexLayouts,
                                     Shader shader, StateBindings const& stateBindings,
                                     RasterState rasterState, DepthState depthState, StencilState stencilState)
    : RenderState(backend, renderTarget, vertexLayouts, shader, stateBindings, rasterState, depthState, stencilState)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    const auto& vulkanBackend = static_cast<VulkanBackend&>(backend);
    const auto& device = vulkanBackend.device();

    std::vector<VkVertexInputBindingDescription> bindingDescriptions {};
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions {};

    u32 nextLocation = 0;
    for (VertexLayout const& vertexLayout : vertexLayouts) {

        u32 bindingIdx = narrow_cast<u32>(bindingDescriptions.size());

        VkVertexInputBindingDescription& bindingDescription = bindingDescriptions.emplace_back();
        bindingDescription.binding = bindingIdx;
        bindingDescription.stride = narrow_cast<u32>(vertexLayout.packedVertexSize());
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        if (vertexLayout.componentCount() > 0) {

            attributeDescriptions.reserve(vertexLayout.components().size());

            u32 currentOffset = 0;

            for (const VertexComponent& component : vertexLayout.components()) {

                VkVertexInputAttributeDescription description = {};
                description.binding = bindingIdx;
                description.location = nextLocation;
                description.offset = currentOffset;

                nextLocation += 1;
                currentOffset += narrow_cast<u32>(vertexComponentSize(component));

                switch (component) {
                case VertexComponent::Position2F:
                case VertexComponent::TexCoord2F:
                    description.format = VK_FORMAT_R32G32_SFLOAT;
                    break;
                case VertexComponent::Position3F:
                case VertexComponent::Normal3F:
                case VertexComponent::Color3F:
                    description.format = VK_FORMAT_R32G32B32_SFLOAT;
                    break;
                case VertexComponent::Tangent4F:
                    description.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    break;
                default:
                    ASSERT_NOT_REACHED();
                }

                attributeDescriptions.push_back(description);
            }
        }
    }

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages {};
    {
        for (auto& file : shader.files()) {

            // TODO: Maybe don't create new modules every time? Currently they are deleted later in this function
            VkShaderModuleCreateInfo moduleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            const std::vector<uint32_t>& spirv = ShaderManager::instance().spirv(file);
            moduleCreateInfo.codeSize = sizeof(uint32_t) * spirv.size();
            moduleCreateInfo.pCode = spirv.data();

            VkShaderModule shaderModule {};
            if (vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                ARKOSE_LOG(Fatal, "Error trying to create shader module");
            }

            VkPipelineShaderStageCreateInfo stageCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageCreateInfo.module = shaderModule;
            stageCreateInfo.pName = "main";

            VkShaderStageFlagBits stageFlags;
            switch (file.type()) {
            case ShaderFileType::Vertex:
                stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case ShaderFileType::Fragment:
                stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case ShaderFileType::Task:
                ARKOSE_ASSERT(vulkanBackend.hasMeshShadingSupport());
                stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT;
                break;
            case ShaderFileType::Mesh:
                ARKOSE_ASSERT(vulkanBackend.hasMeshShadingSupport());
                stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
                break;
            default:
                ASSERT_NOT_REACHED();
            }
            stageCreateInfo.stage = stageFlags;

            shaderStages.push_back(stageCreateInfo);
        }

        // Ensure all named constants across the shader files are compatible with each other
        // Note that this can't be called until we're sure all shaders are compiled, which they
        // definitely should be now after we're set up all the shader modules.
        ShaderManager::instance().ensureCompatibleNamedConstants(shader);
    }

    //
    // Create pipeline layout
    //
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts {};
    for (const BindingSet* bindingSet : stateBindings.orderedBindingSets()) {
        if (auto* vulkanBindingSet = static_cast<const VulkanBindingSet*>(bindingSet)) {
            descriptorSetLayouts.push_back(vulkanBindingSet->descriptorSetLayout);
        } else {
            descriptorSetLayouts.push_back(vulkanBackend.emptyDescriptorSetLayout());
        }
    }

    pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    const auto& pushConstantRange = vulkanBackend.getPushConstantRangeForShader(shader);
    if (pushConstantRange.has_value()) {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange.value();
    } else {
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
    }

    if (vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create pipeline layout");
    }

    //
    // Create pipeline
    //
    VkPipelineVertexInputStateCreateInfo vertInputState = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertInputState.vertexBindingDescriptionCount = narrow_cast<u32>(bindingDescriptions.size());
    vertInputState.pVertexBindingDescriptions = bindingDescriptions.data();
    vertInputState.vertexAttributeDescriptionCount = narrow_cast<u32>(attributeDescriptions.size());
    vertInputState.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    switch (rasterState.primitiveType) {
    case PrimitiveType::Triangles:
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    case PrimitiveType::LineSegments:
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        break;
    case PrimitiveType::Points:
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        break;
    }
    inputAssemblyState.primitiveRestartEnable = VK_FALSE;

    std::vector<VkDynamicState> activeDynamicStates {};
    activeDynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    activeDynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
    //activeDynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);

    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = static_cast<uint32_t>(activeDynamicStates.size());
    dynamicState.pDynamicStates = activeDynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.pViewports = nullptr; // (dynamic state)
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr;// (dynamic state)

    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.depthBiasEnable = rasterState.depthBiasEnabled ? VK_TRUE : VK_FALSE;
    rasterizer.lineWidth = rasterState.lineWidth;

    switch (rasterState.polygonMode) {
    case PolygonMode::Filled:
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        break;
    case PolygonMode::Lines:
        rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        break;
    case PolygonMode::Points:
        rasterizer.polygonMode = VK_POLYGON_MODE_POINT;
        break;
    }

    if (rasterState.backfaceCullingEnabled) {
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    } else {
        rasterizer.cullMode = VK_CULL_MODE_NONE;
    }

    switch (rasterState.frontFace) {
    case TriangleWindingOrder::Clockwise:
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        break;
    case TriangleWindingOrder::CounterClockwise:
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        break;
    }

    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(renderTarget.multisampling());
    multisampling.sampleShadingEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments {};
    for (const auto& attachment : renderTarget.colorAttachments()) {
        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // NOLINT(hicpp-signed-bitwise)
        switch (attachment.blendMode) {
        case RenderTargetBlendMode::None:
            colorBlendAttachment.blendEnable = VK_FALSE;
            break;
        case RenderTargetBlendMode::Additive:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // replace alpha with new value
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            break;
        case RenderTargetBlendMode::AlphaBlending:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            break;
        default:
            ASSERT_NOT_REACHED();
        }
        colorBlendAttachments.push_back(colorBlendAttachment);
    }
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = narrow_cast<u32>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    VkCompareOp depthCompareOp;
    switch (depthState.compareOp) {
    case DepthCompareOp::Less:
        depthCompareOp = VK_COMPARE_OP_LESS;
        break;
    case DepthCompareOp::LessThanEqual:
        depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    case DepthCompareOp::Greater:
        depthCompareOp = VK_COMPARE_OP_GREATER;
        break;
    case DepthCompareOp::GreaterThanEqual:
        depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        break;
    case DepthCompareOp::Equal:
        depthCompareOp = VK_COMPARE_OP_EQUAL;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencilState.depthTestEnable = depthState.testDepth;
    depthStencilState.depthWriteEnable = depthState.writeDepth;
    depthStencilState.depthCompareOp = depthCompareOp;
    depthStencilState.depthBoundsTestEnable = VK_FALSE;
    depthStencilState.minDepthBounds = 0.0f;
    depthStencilState.maxDepthBounds = 1.0f;

    if (stencilState.mode != StencilMode::Disabled) {
        depthStencilState.stencilTestEnable = VK_TRUE;
        switch (stencilState.mode) {
        case StencilMode::AlwaysWrite:
            // Test
            depthStencilState.front.compareOp = VK_COMPARE_OP_ALWAYS;
            depthStencilState.front.compareMask = 0x00;
            // Writing
            depthStencilState.front.passOp = VK_STENCIL_OP_REPLACE;
            depthStencilState.front.reference = stencilState.value;
            depthStencilState.front.writeMask = 0xff;
            break;

        case StencilMode::ReplaceIfGreaterOrEqual:
            // Test
            depthStencilState.front.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
            depthStencilState.front.compareMask = 0xff;
            // Writing
            depthStencilState.front.passOp = VK_STENCIL_OP_REPLACE;
            depthStencilState.front.failOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.depthFailOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.reference = stencilState.value;
            depthStencilState.front.writeMask = 0xff;
            break;

        case StencilMode::PassIfEqual:
            // Test
            depthStencilState.front.compareOp = VK_COMPARE_OP_EQUAL;
            depthStencilState.front.compareMask = 0xff;
            depthStencilState.front.reference = stencilState.value;
            // Writing (in this case, no writing)
            depthStencilState.front.passOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.failOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.depthFailOp = VK_STENCIL_OP_KEEP;
            depthStencilState.front.writeMask = 0x00;
            break;

        default:
            ASSERT_NOT_REACHED();
        }

        // For now, no separate front/back treatment supported
        depthStencilState.back = depthStencilState.front;
    } else {
        depthStencilState.stencilTestEnable = VK_FALSE;
        depthStencilState.front = {};
        depthStencilState.back = {};
    }

    VkGraphicsPipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

    // stages
    pipelineCreateInfo.stageCount = (uint32_t)shaderStages.size();
    pipelineCreateInfo.pStages = shaderStages.data();

    // fixed function stuff
    pipelineCreateInfo.pVertexInputState = &vertInputState;
    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampling;
    pipelineCreateInfo.pDepthStencilState = &depthStencilState;
    pipelineCreateInfo.pColorBlendState = &colorBlending;
    pipelineCreateInfo.pDynamicState = &dynamicState;

    // pipeline layout
    pipelineCreateInfo.layout = pipelineLayout;

    // render pass stuff
    auto& vulkanRenderTarget = static_cast<const VulkanRenderTarget&>(renderTarget);
    pipelineCreateInfo.renderPass = vulkanRenderTarget.compatibleRenderPass;
    pipelineCreateInfo.subpass = 0; // TODO: How should this be handled?

    // extra stuff (optional for this)
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineCreateInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(device, vulkanBackend.pipelineCache(), 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        ARKOSE_LOG(Fatal, "Error trying to create graphics pipeline");
    }

    // Remove shader modules, they are no longer needed after creating the pipeline
    for (auto& stage : shaderStages) {
        vkDestroyShaderModule(device, stage.module, nullptr);
    }
}

VulkanRenderState::~VulkanRenderState()
{
    if (!hasBackend())
        return;
    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    vkDestroyPipeline(vulkanBackend.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(vulkanBackend.device(), pipelineLayout, nullptr);
}

void VulkanRenderState::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    auto& vulkanBackend = static_cast<VulkanBackend&>(backend());
    if (vulkanBackend.hasDebugUtilsSupport()) {

        std::string pipelineName = name + "-pipeline";
        std::string pipelineLayoutName = name + "-pipelineLayout";

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipeline);
            nameInfo.pObjectName = pipelineName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan graphics pipeline resource.");
            }
        }

        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipelineLayout);
            nameInfo.pObjectName = pipelineLayoutName.c_str();

            if (vulkanBackend.debugUtils().vkSetDebugUtilsObjectNameEXT(vulkanBackend.device(), &nameInfo) != VK_SUCCESS) {
                ARKOSE_LOG(Warning, "Could not set debug name for vulkan graphics pipeline layout resource.");
            }
        }
    }
}
