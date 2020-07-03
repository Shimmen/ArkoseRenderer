#include "VulkanCommandList.h"

#include "VulkanBackend.h"
#include "VulkanResources.h"
#include "utility/Logging.h"
#include <stb_image_write.h>

VulkanCommandList::VulkanCommandList(VulkanBackend& backend, VkCommandBuffer commandBuffer)
    : m_backend(backend)
    , m_commandBuffer(commandBuffer)
{
}

void VulkanCommandList::clearTexture(Texture& genColorTexture, ClearColor color)
{
    auto& colorTexture = dynamic_cast<VulkanTexture&>(genColorTexture);
    ASSERT(!colorTexture.hasDepthFormat());

    std::optional<VkImageLayout> originalLayout;
    if (colorTexture.currentLayout != VK_IMAGE_LAYOUT_GENERAL) {
        originalLayout = colorTexture.currentLayout;
        m_backend.transitionImageLayout(colorTexture.image, false, originalLayout.value(), VK_IMAGE_LAYOUT_GENERAL, &m_commandBuffer);
    }

    VkClearColorValue clearValue {};
    clearValue.float32[0] = color.r;
    clearValue.float32[1] = color.g;
    clearValue.float32[2] = color.b;
    clearValue.float32[3] = color.a;

    VkImageSubresourceRange range {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    range.baseMipLevel = 0;
    range.levelCount = colorTexture.mipLevels();

    range.baseArrayLayer = 0;
    range.layerCount = 1;

    vkCmdClearColorImage(m_commandBuffer, colorTexture.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &range);

    if (originalLayout.has_value()) {
        m_backend.transitionImageLayout(colorTexture.image, false, VK_IMAGE_LAYOUT_GENERAL, originalLayout.value(), &m_commandBuffer);
    }
}

void VulkanCommandList::setRenderState(const RenderState& genRenderState, ClearColor clearColor, float clearDepth, uint32_t clearStencil)
{
    if (activeRenderState) {
        LogWarning("setRenderState: already active render state!\n");
        endCurrentRenderPassIfAny();
    }
    auto& renderState = dynamic_cast<const VulkanRenderState&>(genRenderState);
    activeRenderState = &renderState;

    activeRayTracingState = nullptr;
    activeComputeState = nullptr;

    auto& renderTarget = dynamic_cast<const VulkanRenderTarget&>(renderState.renderTarget());

    std::vector<VkClearValue> clearValues {};
    {
        for (auto& attachment : renderTarget.sortedAttachments()) {
            VkClearValue value = {};
            if (attachment.type == RenderTarget::AttachmentType::Depth) {
                value.depthStencil = { clearDepth, clearStencil };
            } else {
                value.color = { { clearColor.r, clearColor.g, clearColor.b, clearColor.a } };
            }
            clearValues.push_back(value);
        }
    }

    VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    // (there is automatic image layout transitions for attached textures, so when we bind the
    //  render target here, make sure to also swap to the new layout in the cache variable)
    for (const auto& [genAttachedTexture, implicitTransitionLayout] : renderTarget.attachedTextures) {
        auto& constAttachedTexture = dynamic_cast<const VulkanTexture&>(*genAttachedTexture);
        auto& attachedTexture = const_cast<VulkanTexture&>(constAttachedTexture); // FIXME: const_cast
        attachedTexture.currentLayout = implicitTransitionLayout;
    }

    // Explicitly transition the layouts of the sampled textures to an optimal layout (if it isn't already)
    {
        for (const Texture* genTexture : renderState.sampledTextures) {
            auto& constTexture = dynamic_cast<const VulkanTexture&>(*genTexture);
            auto& texture = const_cast<VulkanTexture&>(constTexture); // FIXME: const_cast
            if (texture.currentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                m_backend.transitionImageLayout(texture.image, texture.hasDepthFormat(), texture.currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_commandBuffer);
            }
            texture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // TODO: We probably want to support storage images here as well!
        }
    }

    renderPassBeginInfo.renderPass = renderTarget.compatibleRenderPass;
    renderPassBeginInfo.framebuffer = renderTarget.framebuffer;

    auto& targetExtent = renderTarget.extent();
    renderPassBeginInfo.renderArea.offset = { 0, 0 };
    renderPassBeginInfo.renderArea.extent = { targetExtent.width(), targetExtent.height() };

    renderPassBeginInfo.clearValueCount = clearValues.size();
    renderPassBeginInfo.pClearValues = clearValues.data();

    // TODO: Handle subpasses properly!
    vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderState.pipeline);
}

void VulkanCommandList::setRayTracingState(const RayTracingState& genRtState)
{
    if (!backend().hasRtxSupport()) {
        LogErrorAndExit("Trying to set ray tracing state but there is no ray tracing support!\n");
    }

    if (activeRenderState) {
        LogWarning("setRayTracingState: active render state when starting ray tracing.\n");
        endCurrentRenderPassIfAny();
    }

    auto& rtState = dynamic_cast<const VulkanRayTracingState&>(genRtState);
    activeRayTracingState = &rtState;
    activeComputeState = nullptr;

    // Explicitly transition the layouts of the referenced textures to an optimal layout (if it isn't already)
    {
        for (const Texture* texture : rtState.sampledTextures) {
            auto& constVulkanTexture = dynamic_cast<const VulkanTexture&>(*texture);
            auto& vulkanTexture = const_cast<VulkanTexture&>(constVulkanTexture); // FIXME: const_cast
            if (vulkanTexture.currentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                m_backend.transitionImageLayout(vulkanTexture.image, texture->hasDepthFormat(), vulkanTexture.currentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_commandBuffer);
            }
            vulkanTexture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        for (const Texture* texture : rtState.storageImages) {
            auto& constVulkanTexture = dynamic_cast<const VulkanTexture&>(*texture);
            auto& vulkanTexture = const_cast<VulkanTexture&>(constVulkanTexture); // FIXME: const_cast
            if (vulkanTexture.currentLayout != VK_IMAGE_LAYOUT_GENERAL) {
                m_backend.transitionImageLayout(vulkanTexture.image, texture->hasDepthFormat(), vulkanTexture.currentLayout, VK_IMAGE_LAYOUT_GENERAL, &m_commandBuffer);
            }
            vulkanTexture.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
        }
    }

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtState.pipeline);
}

void VulkanCommandList::setComputeState(const ComputeState& genComputeState)
{
    if (activeRenderState) {
        LogWarning("setComputeState: active render state when starting compute state.\n");
        endCurrentRenderPassIfAny();
    }

    auto& computeState = dynamic_cast<const VulkanComputeState&>(genComputeState);
    activeComputeState = &computeState;
    activeRayTracingState = nullptr;

    // Explicitly transition the layouts of the referenced textures to an optimal layout (if it isn't already)
    for (const Texture* texture : computeState.storageImages) {
        auto& constVulkanTexture = dynamic_cast<const VulkanTexture&>(*texture);
        auto& vulkanTexture = const_cast<VulkanTexture&>(constVulkanTexture); // FIXME: const_cast
        if (vulkanTexture.currentLayout != VK_IMAGE_LAYOUT_GENERAL) {
            m_backend.transitionImageLayout(vulkanTexture.image, texture->hasDepthFormat(), vulkanTexture.currentLayout, VK_IMAGE_LAYOUT_GENERAL, &m_commandBuffer);
        }
        vulkanTexture.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computeState.pipeline);
}

void VulkanCommandList::bindSet(BindingSet& bindingSet, uint32_t index)
{
    if (!activeRenderState && !activeRayTracingState && !activeComputeState) {
        LogErrorAndExit("bindSet: no active render or compute or ray tracing state to bind to!\n");
    }

    ASSERT(!(activeRenderState && activeRayTracingState && activeComputeState));

    VkPipelineLayout pipelineLayout;
    VkPipelineBindPoint bindPoint;

    if (activeRenderState) {
        pipelineLayout = activeRenderState->pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
    if (activeComputeState) {
        pipelineLayout = activeComputeState->pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    }
    if (activeRayTracingState) {
        pipelineLayout = activeRayTracingState->pipelineLayout;
        bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_NV;
    }

    auto& vulkanBindingSet = dynamic_cast<VulkanBindingSet&>(bindingSet);
    vkCmdBindDescriptorSets(m_commandBuffer, bindPoint, pipelineLayout, index, 1, &vulkanBindingSet.descriptorSet, 0, nullptr);
}

void VulkanCommandList::pushConstants(ShaderStage shaderStage, void* data, size_t size, size_t byteOffset)
{
    if (!activeRenderState && !activeRayTracingState && !activeComputeState) {
        LogErrorAndExit("pushConstants: no active render or compute or ray tracing state to bind to!\n");
    }

    ASSERT(!(activeRenderState && activeRayTracingState && activeComputeState));

    VkPipelineLayout pipelineLayout;
    if (activeRenderState) {
        pipelineLayout = activeRenderState->pipelineLayout;
    }
    if (activeComputeState) {
        pipelineLayout = activeComputeState->pipelineLayout;
    }
    if (activeRayTracingState) {
        pipelineLayout = activeRayTracingState->pipelineLayout;
    }

    // TODO: This isn't the only occurance of this shady table. We probably want a function for doing this translation!
    VkShaderStageFlags stageFlags = 0u;
    if (shaderStage & ShaderStageVertex)
        stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (shaderStage & ShaderStageFragment)
        stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (shaderStage & ShaderStageCompute)
        stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (shaderStage & ShaderStageRTRayGen)
        stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_NV;
    if (shaderStage & ShaderStageRTMiss)
        stageFlags |= VK_SHADER_STAGE_MISS_BIT_NV;
    if (shaderStage & ShaderStageRTClosestHit)
        stageFlags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;

    vkCmdPushConstants(m_commandBuffer, pipelineLayout, stageFlags, byteOffset, size, data);
}

void VulkanCommandList::draw(Buffer& vertexBuffer, uint32_t vertexCount)
{
    if (!activeRenderState) {
        LogErrorAndExit("draw: no active render state!\n");
    }

    VkBuffer vertBuffer = dynamic_cast<VulkanBuffer&>(vertexBuffer).buffer;

    VkBuffer vertexBuffers[] = { vertBuffer };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(m_commandBuffer, vertexCount, 1, 0, 0);
}

void VulkanCommandList::drawIndexed(Buffer& vertexBuffer, Buffer& indexBuffer, uint32_t indexCount, IndexType indexType, uint32_t instanceIndex)
{
    if (!activeRenderState) {
        LogErrorAndExit("drawIndexed: no active render state!\n");
    }

    VkBuffer vertBuffer = dynamic_cast<VulkanBuffer&>(vertexBuffer).buffer;
    VkBuffer idxBuffer = dynamic_cast<VulkanBuffer&>(indexBuffer).buffer;

    VkBuffer vertexBuffers[] = { vertBuffer };
    VkDeviceSize offsets[] = { 0 };

    VkIndexType vkIndexType;
    switch (indexType) {
    case IndexType::UInt16:
        vkIndexType = VK_INDEX_TYPE_UINT16;
        break;
    case IndexType::UInt32:
        vkIndexType = VK_INDEX_TYPE_UINT32;
        break;
    }

    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(m_commandBuffer, idxBuffer, 0, vkIndexType);
    vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, 0, 0, instanceIndex);
}

void VulkanCommandList::rebuildTopLevelAcceratationStructure(TopLevelAS& tlas)
{
    if (!backend().hasRtxSupport())
        LogErrorAndExit("Trying to rebuild a top level acceleration structure but there is no ray tracing support!\n");

    auto& vulkanTlas = dynamic_cast<VulkanTopLevelAS&>(tlas);

    // TODO: Maybe don't throw the allocation away (when building the first time), so we can reuse it here?
    //  However, it's a different size, though! So maybe not. Or if we use the max(build, rebuild) size?
    VmaAllocation scratchAllocation;
    VkBuffer scratchBuffer = backend().rtx().createScratchBufferForAccelerationStructure(vulkanTlas.accelerationStructure, true, scratchAllocation);

    VmaAllocation instanceAllocation;
    VkBuffer instanceBuffer = backend().rtx().createInstanceBuffer(tlas.instances(), instanceAllocation);

    VkAccelerationStructureInfoNV buildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
    buildInfo.instanceCount = tlas.instanceCount();
    buildInfo.geometryCount = 0;
    buildInfo.pGeometries = nullptr;

    m_backend.rtx().vkCmdBuildAccelerationStructureNV(
        m_commandBuffer,
        &buildInfo,
        instanceBuffer, 0,
        VK_TRUE,
        vulkanTlas.accelerationStructure,
        vulkanTlas.accelerationStructure,
        scratchBuffer, 0);

    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    vmaDestroyBuffer(m_backend.globalAllocator(), scratchBuffer, scratchAllocation);

    // Delete the old instance buffer & replace with the new one
    ASSERT(vulkanTlas.associatedBuffers.size() == 1);
    auto& [prevInstanceBuf, prevInstanceAlloc] = vulkanTlas.associatedBuffers[0];
    vmaDestroyBuffer(m_backend.globalAllocator(), prevInstanceBuf, prevInstanceAlloc);
    vulkanTlas.associatedBuffers[0] = { instanceBuffer, instanceAllocation };
}

void VulkanCommandList::traceRays(Extent2D extent)
{
    if (!activeRayTracingState)
        LogErrorAndExit("traceRays: no active ray tracing state!\n");
    if (!backend().hasRtxSupport())
        LogErrorAndExit("Trying to trace rays but there is no ray tracing support!\n");

    VkBuffer sbtBuffer = dynamic_cast<const VulkanRayTracingState&>(*activeRayTracingState).sbtBuffer;

    uint32_t baseAlignment = backend().rtx().properties().shaderGroupBaseAlignment;

    uint32_t raygenOffset = 0; // we always start with raygen
    uint32_t raygenStride = baseAlignment; // since we have no data => TODO!
    size_t numRaygenShaders = 1; // for now, always just one

    uint32_t hitGroupOffset = raygenOffset + (numRaygenShaders * raygenStride);
    uint32_t hitGroupStride = baseAlignment; // since we have no data and a single shader for now => TODO! ALSO CONSIDER IF THIS SHOULD SIMPLY BE PASSED IN TO HERE?!
    size_t numHitGroups = activeRayTracingState->shaderBindingTable().hitGroups().size();

    uint32_t missOffset = hitGroupOffset + (numHitGroups * hitGroupStride);
    uint32_t missStride = baseAlignment; // since we have no data => TODO!

    backend().rtx().vkCmdTraceRaysNV(m_commandBuffer,
                                     sbtBuffer, raygenOffset,
                                     sbtBuffer, missOffset, missStride,
                                     sbtBuffer, hitGroupOffset, hitGroupStride,
                                     VK_NULL_HANDLE, 0, 0,
                                     extent.width(), extent.height(), 1);
}

void VulkanCommandList::dispatch(Extent3D globalSize, Extent3D localSize)
{
    uint32_t x = (globalSize.width() + localSize.width() - 1) / localSize.width();
    uint32_t y = (globalSize.height() + localSize.height() - 1) / localSize.height();
    uint32_t z = (globalSize.depth() + localSize.depth() - 1) / localSize.depth();
    dispatch(x, y, z);
}

void VulkanCommandList::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    if (!activeComputeState) {
        LogErrorAndExit("Trying to dispatch compute but there is no active compute state!\n");
    }
    vkCmdDispatch(m_commandBuffer, x, y, z);
}

void VulkanCommandList::waitEvent(uint8_t eventId, PipelineStage stage)
{
    VkEvent event = getEvent(eventId);
    VkPipelineStageFlags flags = stageFlags(stage);

    vkCmdWaitEvents(m_commandBuffer, 1, &event,
                    flags, flags, // TODO: Might be required that we have different stages here later!
                    0, nullptr,
                    0, nullptr,
                    0, nullptr);
}

void VulkanCommandList::resetEvent(uint8_t eventId, PipelineStage stage)
{
    VkEvent event = getEvent(eventId);
    vkCmdResetEvent(m_commandBuffer, event, stageFlags(stage));
}

void VulkanCommandList::signalEvent(uint8_t eventId, PipelineStage stage)
{
    VkEvent event = getEvent(eventId);
    vkCmdSetEvent(m_commandBuffer, event, stageFlags(stage));
}

void VulkanCommandList::saveTextureToFile(const Texture& texture, const std::string& filePath)
{
    const VkFormat targetFormat = VK_FORMAT_R8G8B8A8_UNORM;

    auto& srcTex = dynamic_cast<const VulkanTexture&>(texture);
    VkImageLayout prevSrcLayout = srcTex.currentLayout;
    VkImage srcImage = srcTex.image;

    VkImageCreateInfo imageCreateInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = targetFormat;
    imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageCreateInfo.extent.width = texture.extent().width();
    imageCreateInfo.extent.height = texture.extent().height();
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocCreateInfo {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    VkImage dstImage;
    VmaAllocation dstAllocation;
    VmaAllocationInfo dstAllocationInfo;
    if (vmaCreateImage(m_backend.globalAllocator(), &imageCreateInfo, &allocCreateInfo, &dstImage, &dstAllocation, &dstAllocationInfo) != VK_SUCCESS) {
        LogErrorAndExit("Failed to create temp image for screenshot\n");
    }

    bool success = m_backend.issueSingleTimeCommand([&](VkCommandBuffer cmdBuffer) {
        transitionImageLayoutDEBUG(dstImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);
        transitionImageLayoutDEBUG(srcImage, prevSrcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);

        VkImageCopy imageCopyRegion {};
        imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.srcSubresource.layerCount = 1;
        imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.dstSubresource.layerCount = 1;
        imageCopyRegion.extent.width = texture.extent().width();
        imageCopyRegion.extent.height = texture.extent().height();
        imageCopyRegion.extent.depth = 1;

        vkCmdCopyImage(cmdBuffer,
                       srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &imageCopyRegion);

        // Transition destination image to general layout, which is the required layout for mapping the image memory
        transitionImageLayoutDEBUG(dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);
        transitionImageLayoutDEBUG(srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, prevSrcLayout, VK_IMAGE_ASPECT_COLOR_BIT, cmdBuffer);
    });

    if (!success) {
        LogError("Failed to setup screenshot image & data...\n");
    }

    // Get layout of the image (including row pitch/stride)
    VkImageSubresource subResource;
    subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subResource.mipLevel = 0;
    subResource.arrayLayer = 0;
    VkSubresourceLayout subResourceLayout;
    vkGetImageSubresourceLayout(device(), dstImage, &subResource, &subResourceLayout);

    char* data;
    vkMapMemory(device(), dstAllocationInfo.deviceMemory, 0, VK_WHOLE_SIZE, 0, (void**)&data);
    data += subResourceLayout.offset;

    bool shouldSwizzleRedAndBlue = (srcTex.vkFormat == VK_FORMAT_B8G8R8A8_SRGB) || (srcTex.vkFormat == VK_FORMAT_B8G8R8A8_UNORM) || (srcTex.vkFormat == VK_FORMAT_B8G8R8A8_SNORM);
    if (shouldSwizzleRedAndBlue) {
        int numPixels = texture.extent().width() * texture.extent().height();
        for (int i = 0; i < numPixels; ++i) {
            char tmp = data[4 * i + 0];
            data[4 * i + 0] = data[4 * i + 2];
            data[4 * i + 2] = tmp;
        }
    }

    if (!stbi_write_png(filePath.c_str(), texture.extent().width(), texture.extent().height(), 4, data, subResourceLayout.rowPitch)) {
        LogError("Failed to write screenshot to file...\n");
    }

    vkUnmapMemory(device(), dstAllocationInfo.deviceMemory);
    vmaDestroyImage(m_backend.globalAllocator(), dstImage, dstAllocation);
}

void VulkanCommandList::debugBarrier()
{
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    vkCmdPipelineBarrier(m_commandBuffer, sourceStage, destinationStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanCommandList::endNode(Badge<class VulkanBackend>)
{
    endCurrentRenderPassIfAny();
    debugBarrier(); // TODO: We probably don't need to do this..?
}

void VulkanCommandList::endCurrentRenderPassIfAny()
{
    if (activeRenderState) {
        vkCmdEndRenderPass(m_commandBuffer);
        activeRenderState = nullptr;
    }
}

VkEvent VulkanCommandList::getEvent(uint8_t eventId)
{
    const auto& events = m_backend.m_events;

    if (eventId >= events.size()) {
        LogErrorAndExit("Event of id %u requested, which is >= than the number of created events (%u)\n", eventId, events.size());
    }
    return events[eventId];
}

VkPipelineStageFlags VulkanCommandList::stageFlags(PipelineStage stage) const
{
    switch (stage) {
    case PipelineStage::Host:
        return VK_PIPELINE_STAGE_HOST_BIT;
    case PipelineStage::RayTracing:
        return VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;
    default:
        ASSERT(false);
    }
}

void VulkanCommandList::transitionImageLayoutDEBUG(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags imageAspectMask, VkCommandBuffer commandBuffer) const
{
    VkImageMemoryBarrier imageMemoryBarrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

    imageMemoryBarrier.image = image;
    imageMemoryBarrier.oldLayout = oldLayout;
    imageMemoryBarrier.newLayout = newLayout;

    imageMemoryBarrier.subresourceRange.aspectMask = imageAspectMask;
    imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
    imageMemoryBarrier.subresourceRange.layerCount = 1;
    imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
    imageMemoryBarrier.subresourceRange.levelCount = 1;

    // Just do the strictest possible barrier so it should at least be valid, albeit slow.
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    VkPipelineStageFlagBits srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlagBits dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         srcStageMask, dstStageMask,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &imageMemoryBarrier);
}
