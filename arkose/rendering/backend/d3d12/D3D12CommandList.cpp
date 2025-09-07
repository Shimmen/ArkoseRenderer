#include "D3D12CommandList.h"

#include "utility/Profiling.h"
#include "rendering/backend/d3d12/D3D12Backend.h"
#include "rendering/backend/d3d12/D3D12Common.h"
#include "rendering/backend/d3d12/D3D12BindingSet.h"
#include "rendering/backend/d3d12/D3D12ComputeState.h"
#include "rendering/backend/d3d12/D3D12Buffer.h"
#include "rendering/backend/d3d12/D3D12RenderState.h"
#include "rendering/backend/d3d12/D3D12RenderTarget.h"
#include "rendering/backend/d3d12/D3D12Texture.h"
#include <d3d12.h>
#include <pix.h> // for PIXBeginEvent & PIXEndEvent

D3D12CommandList::D3D12CommandList(D3D12Backend& backend, ID3D12GraphicsCommandList* d3d12CommandList)
    : m_backend(backend)
    , m_commandList(d3d12CommandList)
{
}

void D3D12CommandList::fillBuffer(Buffer&, u32 fillValue)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::clearTexture(Texture& genColorTexture, ClearValue clearValue)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::copyTexture(Texture& srcTexture, Texture& dstTexture, u32 srcMip, u32 dstMip)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    Extent3D srcExtent = srcTexture.extent3DAtMip(srcMip);
    Extent3D dstExtent = dstTexture.extent3DAtMip(dstMip);

    if (srcExtent == dstExtent) {

        auto& d3d12srcTexture = static_cast<D3D12Texture&>(srcTexture);
        auto& d3d12dstTexture = static_cast<D3D12Texture&>(dstTexture);

        D3D12_TEXTURE_COPY_LOCATION srcCopyLocation;
        srcCopyLocation.pResource = d3d12srcTexture.textureResource.Get();
        srcCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcCopyLocation.SubresourceIndex = srcMip; // is this always correct? e.g. in case of texture arrays?

        D3D12_TEXTURE_COPY_LOCATION dstCopyLocation;
        dstCopyLocation.pResource = d3d12dstTexture.textureResource.Get();
        dstCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstCopyLocation.SubresourceIndex = dstMip; // is this always correct? e.g. in case of texture arrays?

        m_commandList->CopyTextureRegion(&dstCopyLocation, 0, 0, 0,
                                         &srcCopyLocation, nullptr);

        // any barrier needed? see https://asawicki.info/news_1722_secrets_of_direct3d_12_copies_to_the_same_buffer

    } else {

        // Perform a blit, backed by an authored compute shader

        NOT_YET_IMPLEMENTED();

        // TODO - something like this:
        //backend().blitTexture(dstTexture, dstMip, srcTexture, srcMip);
        //textureMipWriteBarrier(dstTexture, dstMip);

        // More info (these are specifically about mipmap generation, but the core problem is essentially the same):
        // https://slindev.com/d3d12-texture-mipmap-generation/
        // https://github.com/microsoft/DirectXTex/wiki/GenerateMipMaps

    }
}

void D3D12CommandList::generateMipmaps(Texture& genTexture)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    auto& texture = static_cast<D3D12Texture&>(genTexture);

    if (!texture.hasMipmaps()) {
        ARKOSE_LOG(Error, "generateMipmaps called on command list for texture which doesn't have mipmaps. Ignoring request.");
        return;
    }

    beginDebugLabel(fmt::format("Generate Mipmaps ({}x{})", genTexture.extent().width(), genTexture.extent().height()));

    u32 mipLevels = texture.mipLevels();
    for (u32 targetMipLevel = 1; targetMipLevel < mipLevels; ++targetMipLevel) {
        u32 sourceMipLevel = targetMipLevel - 1;
        copyTexture(texture, texture, sourceMipLevel, targetMipLevel);
    }

    endDebugLabel();
}

void D3D12CommandList::executeBufferCopyOperations(std::vector<BufferCopyOperation> copyOperations)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (copyOperations.size() == 0) {
        return;
    }

    beginDebugLabel(fmt::format("Execute buffer copy operations (x{})", copyOperations.size()));

    for (BufferCopyOperation const& copyOperation : copyOperations) {

        if (copyOperation.size == 0) {
            continue;
        }

        if (std::holds_alternative<BufferCopyOperation::BufferDestination>(copyOperation.destination)) {
            auto const& copyDestination = std::get<BufferCopyOperation::BufferDestination>(copyOperation.destination);

            D3D12Buffer& srcBuffer = static_cast<D3D12Buffer&>(*copyOperation.srcBuffer);
            D3D12Buffer& dstBuffer = static_cast<D3D12Buffer&>(*copyDestination.buffer);

            constexpr D3D12_RESOURCE_STATES expectedResourceState = D3D12_RESOURCE_STATE_COPY_DEST;
            if (dstBuffer.resourceState != expectedResourceState) {
                D3D12_RESOURCE_BARRIER resourceBarrier = createResourceTransitionBarrier(dstBuffer, expectedResourceState);
                m_commandList->ResourceBarrier(1, &resourceBarrier);
                dstBuffer.resourceState = expectedResourceState;
            }

            m_commandList->CopyBufferRegion(dstBuffer.bufferResource.Get(), copyDestination.offset,
                                            srcBuffer.bufferResource.Get(), copyOperation.srcOffset,
                                            copyOperation.size);

        } else if (std::holds_alternative<BufferCopyOperation::TextureDestination>(copyOperation.destination)) {
            auto const& copyDestination = std::get<BufferCopyOperation::TextureDestination>(copyOperation.destination);

            D3D12Texture& dstTexture = *static_cast<D3D12Texture*>(copyDestination.texture);
            ID3D12Resource* srcBuffer = static_cast<D3D12Buffer*>(copyOperation.srcBuffer)->bufferResource.Get();

            (void)dstTexture;
            (void)srcBuffer;
            NOT_YET_IMPLEMENTED();

        } else {
            ASSERT_NOT_REACHED();
        }
    }

    endDebugLabel();
}

void D3D12CommandList::beginRendering(const RenderState& genRenderState, bool autoSetViewport)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::beginRendering(const RenderState& genRenderState, ClearValue clearValue, bool autoSetViewport)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (m_activeRenderState) {
        ARKOSE_LOG(Warning, "beginRendering: already active render state!");
        m_activeRenderState = nullptr;
    }

    auto& renderState = static_cast<const D3D12RenderState&>(genRenderState);
    m_activeRenderState = &renderState;
    //m_activeRayTracingState = nullptr;
    m_activeComputeState = nullptr;

    auto& renderTarget = static_cast<const D3D12RenderTarget&>(renderState.renderTarget());

    // Ensure all referenced resources are in a suitable resource state
    {
        std::vector<D3D12_RESOURCE_BARRIER> resourceBarriers {};

        for (RenderTarget::Attachment const& attachment : renderTarget.colorAttachments()) {
            auto& attachedTexture = static_cast<D3D12Texture&>(*attachment.texture);

            constexpr D3D12_RESOURCE_STATES targetResourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            if (attachedTexture.resourceState != targetResourceState) {
                D3D12_RESOURCE_BARRIER barrier = createResourceTransitionBarrier(attachedTexture, targetResourceState);
                resourceBarriers.push_back(barrier);
                attachedTexture.resourceState = targetResourceState;
            }
        }

        if (renderTarget.hasDepthAttachment()) {
            auto& attachedDepthTexture = static_cast<D3D12Texture&>(*renderTarget.depthAttachment()->texture);

            D3D12_RESOURCE_STATES targetResourceState = D3D12_RESOURCE_STATE_DEPTH_READ;
            if (renderTarget.depthAttachment()->loadOp == LoadOp::Clear || renderState.depthState().writeDepth) {
                targetResourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            }

            if (attachedDepthTexture.resourceState != targetResourceState) {
                D3D12_RESOURCE_BARRIER barrier = createResourceTransitionBarrier(attachedDepthTexture, targetResourceState);
                resourceBarriers.push_back(barrier);
                attachedDepthTexture.resourceState = targetResourceState;
            }
        }

        createTransitionBarriersForAllReferencedResources(renderState.stateBindings(), resourceBarriers);

        if (resourceBarriers.size() > 0) {
            m_commandList->ResourceBarrier(narrow_cast<u32>(resourceBarriers.size()), resourceBarriers.data());
        }
    }

    // Replace render target handles with the current swapchain one, if relevant
    renderTarget.forEachAttachmentInOrder([&](const RenderTarget::Attachment& attachment) {
        if (attachment.texture == backend().placeholderSwapchainTexture()) {
            ARKOSE_ASSERT(attachment.type != RenderTarget::AttachmentType::Depth);

            u32 attachmentIdx = toUnderlying(attachment.type);
            renderTarget.colorRenderTargetHandles[attachmentIdx] = backend().currentSwapchainRenderTargetHandle();
        }
    });

    renderTarget.forEachAttachmentInOrder([&](const RenderTarget::Attachment& attachment) {
        if (attachment.loadOp == LoadOp::Clear) {
            if (attachment.type == RenderTarget::AttachmentType::Depth) {
                m_commandList->ClearDepthStencilView(renderTarget.depthStencilRenderTargetHandle,
                                                     D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                                     clearValue.depth, narrow_cast<u8>(clearValue.stencil),
                                                     0, nullptr);
            } else {
                u32 attachmentIdx = toUnderlying(attachment.type);
                m_commandList->ClearRenderTargetView(renderTarget.colorRenderTargetHandles[attachmentIdx],
                                                     &clearValue.color.r,
                                                     0, nullptr);
            }
        }
    });

    constexpr bool singleHandleToDescriptorRange = false; // TODO: Can we set this to true? Not sure..
    m_commandList->OMSetRenderTargets(narrow_cast<UINT>(renderTarget.colorAttachmentCount()), renderTarget.colorRenderTargetHandles, singleHandleToDescriptorRange,
                                      renderTarget.hasDepthAttachment() ? &renderTarget.depthStencilRenderTargetHandle : nullptr);

    m_commandList->SetPipelineState(renderState.pso.Get());

    switch (renderState.rasterState().primitiveType) {
    case PrimitiveType::Triangles:
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        break;
    case PrimitiveType::LineSegments:
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        break;
    case PrimitiveType::Points:
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    m_commandList->SetGraphicsRootSignature(renderState.rootSignature.Get());

    renderState.stateBindings().forEachBindingSet([&](u32 setIndex, BindingSet& bindingSet) {
        // TODO: Maybe reserve idx0 for "push constants"? Lower indices should be used for data that changes more often according to the D3D12 devs / Microsoft.
        i32 rootParameterIdx = setIndex;

        auto& d3d12BindingSet = static_cast<D3D12BindingSet&>(bindingSet);
        m_commandList->SetGraphicsRootDescriptorTable(rootParameterIdx, d3d12BindingSet.descriptorTableAllocation.firstGpuDescriptor);
    });

    if (autoSetViewport) {
        setViewport({ 0, 0 }, renderTarget.extent().asIntVector());
    }

    if (renderState.stencilState().mode != StencilMode::Disabled) {
        m_commandList->OMSetStencilRef(renderState.stencilState().value);
    }
}

void D3D12CommandList::endRendering()
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    m_activeRenderState = nullptr;
}

void D3D12CommandList::clearRenderTargetAttachment(RenderTarget::AttachmentType attachmentType, Rect2D clearRect, ClearValue clearValue)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::setRayTracingState(const RayTracingState& rtState)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::setComputeState(const ComputeState& genComputeState)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (m_activeRenderState) {
        ARKOSE_LOG(Warning, "setComputeState: active render state when starting compute state.");
        endRendering();
    }

    auto& computeState = static_cast<const D3D12ComputeState&>(genComputeState);
    m_activeComputeState = &computeState;
    //m_activeRayTracingState = nullptr;

    // Ensure all referenced resources are in a suitable resource state
    {
        std::vector<D3D12_RESOURCE_BARRIER> resourceBarriers {};
        createTransitionBarriersForAllReferencedResources(computeState.stateBindings(), resourceBarriers);

        if (resourceBarriers.size() > 0) {
            m_commandList->ResourceBarrier(narrow_cast<u32>(resourceBarriers.size()), resourceBarriers.data());
        }
    }

    m_commandList->SetPipelineState(computeState.pso.Get());
    m_commandList->SetComputeRootSignature(computeState.rootSignature.Get());

    computeState.stateBindings().forEachBindingSet([&](u32 setIndex, BindingSet& bindingSet) {
        i32 rootParameterIdx = setIndex;
        auto& d3d12BindingSet = static_cast<D3D12BindingSet&>(bindingSet);
        m_commandList->SetComputeRootDescriptorTable(rootParameterIdx, d3d12BindingSet.descriptorTableAllocation.firstGpuDescriptor);
    });
}

void D3D12CommandList::evaluateUpscaling(UpscalingState const&, UpscalingParameters)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::evaluateExternalFeature(ExternalFeature const&, void* externalFeatureEvaluateParams)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::bindTextureSet(BindingSet&, u32 index)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::setNamedUniform(const std::string& name, void const* data, size_t size)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    // We always use CBV 0 for named constants
    constexpr UINT rootParameterIndex = 0;

    ARKOSE_ASSERT(size % sizeof(u32) == 0);
    UINT num32bitConstants = narrow_cast<UINT>(size / sizeof(UINT));

    std::optional<u32> constantOffset = m_activeRenderState->namedConstantLookup().lookupConstantOffset(name, size);
    if (!constantOffset.has_value()) {
        ARKOSE_LOG(Error, "D3D12CommandList: failed to look up constant with name '{}' and size {}, ignoring.", name, size);
        return;
    }

    UINT offset = static_cast<UINT>(constantOffset.value());

    if (m_activeRenderState) {
        m_commandList->SetGraphicsRoot32BitConstants(rootParameterIndex, num32bitConstants, data, offset);
    } else if (m_activeComputeState) {
        m_commandList->SetComputeRoot32BitConstants(rootParameterIndex, num32bitConstants, data, offset);
    } else {
        NOT_YET_IMPLEMENTED();
    }
}

void D3D12CommandList::draw(u32 vertexCount, u32 firstVertex)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!m_activeRenderState) {
        ARKOSE_LOG(Fatal, "draw: no active render state!");
    }
    if (m_boundVertexBuffer == nullptr) {
        ARKOSE_LOG(Fatal, "draw: no bound vertex buffer!");
    }

    m_commandList->DrawInstanced(vertexCount, 1, firstVertex, 0);
}

void D3D12CommandList::drawIndexed(u32 indexCount, u32 instanceIndex)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (!m_activeRenderState) {
        ARKOSE_LOG(Fatal, "draw: no active render state!");
    }
    if (m_boundVertexBuffer == nullptr) {
        ARKOSE_LOG(Fatal, "draw: no bound vertex buffer!");
    }
    if (m_boundIndexBuffer == nullptr) {
        ARKOSE_LOG(Fatal, "draw: no bound index buffer!");
    }

    m_commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, instanceIndex);
}

void D3D12CommandList::drawIndirect(const Buffer& indirectBuffer, const Buffer& countBuffer)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::drawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::drawMeshTasksIndirect(Buffer const& indirectBuffer, u32 indirectDataStride, u32 indirectDataOffset,
                                             Buffer const& countBuffer, u32 countDataOffset)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::setViewport(ivec2 origin, ivec2 size)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    ARKOSE_ASSERT(origin.x >= 0);
    ARKOSE_ASSERT(origin.y >= 0);
    ARKOSE_ASSERT(size.x > 0);
    ARKOSE_ASSERT(size.x > 0);

    D3D12_VIEWPORT viewport {};
    viewport.TopLeftX = static_cast<float>(origin.x);
    viewport.TopLeftY = static_cast<float>(origin.y);
    viewport.Width = static_cast<float>(size.x);
    viewport.Height = static_cast<float>(size.y);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    // TODO: Allow independent scissor control
    D3D12_RECT scissorRect {};
    scissorRect.left = LONG(origin.x);
    scissorRect.top = LONG(origin.y);
    scissorRect.right = LONG(origin.x + size.x);
    scissorRect.bottom = LONG(origin.y + size.y);

    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);
}

void D3D12CommandList::setDepthBias(float constantFactor, float slopeFactor)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::bindVertexBuffer(Buffer const& vertexBuffer, size_t stride, u32 bindingIdx)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (vertexBuffer.usage() != Buffer::Usage::Vertex) {
        ARKOSE_LOG(Fatal, "bindVertexBuffer: not a vertex buffer!");
    }

    auto const& d3d12Buffer = static_cast<D3D12Buffer const&>(vertexBuffer);
    ID3D12Resource* d3d12BufferResource = d3d12Buffer.bufferResource.Get();

    constexpr D3D12_RESOURCE_STATES targetResourceState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    if (d3d12Buffer.resourceState != targetResourceState) {
        D3D12_RESOURCE_BARRIER resourceBarrier = createResourceTransitionBarrier(d3d12Buffer, targetResourceState);
        m_commandList->ResourceBarrier(1, &resourceBarrier);
        d3d12Buffer.resourceState = targetResourceState;
    }

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    vertexBufferView.BufferLocation = d3d12BufferResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = narrow_cast<UINT>(vertexBuffer.size());
    vertexBufferView.StrideInBytes = narrow_cast<UINT>(stride);

    m_commandList->IASetVertexBuffers(bindingIdx, 1, &vertexBufferView);

    m_boundVertexBuffer = d3d12BufferResource;
}

void D3D12CommandList::bindIndexBuffer(Buffer const& indexBuffer, IndexType indexType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (indexBuffer.usage() != Buffer::Usage::Index) {
        ARKOSE_LOG(Fatal, "bindIndexBuffer: not an index buffer!");
    }

    auto const& d3d12Buffer = static_cast<D3D12Buffer const&>(indexBuffer);
    ID3D12Resource* d3d12BufferResource = d3d12Buffer.bufferResource.Get();

    constexpr D3D12_RESOURCE_STATES targetResourceState = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    if (d3d12Buffer.resourceState != targetResourceState) {
        D3D12_RESOURCE_BARRIER resourceBarrier = createResourceTransitionBarrier(d3d12Buffer, targetResourceState);
        m_commandList->ResourceBarrier(1, &resourceBarrier);
        d3d12Buffer.resourceState = targetResourceState;
    }

    D3D12_INDEX_BUFFER_VIEW indexBufferView;
    indexBufferView.BufferLocation = d3d12BufferResource->GetGPUVirtualAddress();
    indexBufferView.SizeInBytes = static_cast<UINT>(indexBuffer.size());

    switch (indexType) {
    case IndexType::UInt16:
        indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        break;
    case IndexType::UInt32:
        indexBufferView.Format = DXGI_FORMAT_R32_UINT;
        break;
    default:
        ASSERT_NOT_REACHED();
        break;
    }

    m_commandList->IASetIndexBuffer(&indexBufferView);

    m_boundIndexBuffer = d3d12BufferResource;
}

void D3D12CommandList::issueDrawCall(DrawCallDescription const& drawCall)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::buildTopLevelAcceratationStructure(TopLevelAS& tlas, AccelerationStructureBuildType buildType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::buildBottomLevelAcceratationStructure(BottomLevelAS& blas, AccelerationStructureBuildType buildType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::copyBottomLevelAcceratationStructure(BottomLevelAS& dst, BottomLevelAS const& src)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

bool D3D12CommandList::compactBottomLevelAcceratationStructure(BottomLevelAS& blas)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::traceRays(Extent2D extent)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::slowBlockingReadFromBuffer(const Buffer& buffer, size_t offset, size_t size, void* dst)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::debugBarrier()
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::beginDebugLabel(std::string const& scopeName)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

#if defined(TRACY_ENABLE)
    auto tracyScope = std::make_unique<tracy::D3D12ZoneScope>(backend().tracyD3D12Context(),
                                                              TracyLine,
                                                              TracyFile, strlen(TracyFile),
                                                              TracyFunction, strlen(TracyFunction),
                                                              scopeName.c_str(), scopeName.size(),
                                                              m_commandList, true);
    m_tracyDebugLabelStack.push_back(std::move(tracyScope));
#endif

    // From the RenderDoc documentation (https://renderdoc.org/docs/how/how_annotate_capture.html):
    //   1 for the first parameter means the data is an ANSI string. Pass 0 for a wchar string. the length should include the NULL terminator
    //m_commandList->BeginEvent(1u, scopeName.c_str(), scopeName.size());
    // However, if we use that as-is we get spammed by validation, saying:
    //  "BeginEvent is a diagnostic API used by debugging tools for D3D. Developers should use PIXBeginEvent"
    // which is fair enough because it's documented as internal only and not for use.

    PIXBeginEvent(m_commandList, 0x333333, scopeName.c_str());
}

void D3D12CommandList::endDebugLabel()
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

#if defined(TRACY_ENABLE)
    ARKOSE_ASSERT(m_tracyDebugLabelStack.size() > 0);
    m_tracyDebugLabelStack.pop_back();
#endif

    //m_commandList->EndEvent();
    PIXEndEvent(m_commandList);
}

void D3D12CommandList::textureWriteBarrier(Texture const& texture)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    auto const& d3d12Texture = static_cast<D3D12Texture const&>(texture);
    ARKOSE_ASSERT(d3d12Texture.storageCapable());

    D3D12_RESOURCE_BARRIER resourceBarrier {};
    resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrier.UAV.pResource = d3d12Texture.textureResource.Get();
    m_commandList->ResourceBarrier(1, &resourceBarrier);
}

void D3D12CommandList::textureMipWriteBarrier(Texture const& texture, u32 mip)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    textureWriteBarrier(texture);
}

void D3D12CommandList::bufferWriteBarrier(std::vector<Buffer const*> buffers)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    std::vector<D3D12_RESOURCE_BARRIER> resourceBarriers {};
    for (Buffer const* buffer : buffers) {
        if (auto const* d3d12Buffer = static_cast<D3D12Buffer const*>(buffer)) {
            ARKOSE_ASSERT(d3d12Buffer->storageCapable());

            D3D12_RESOURCE_BARRIER resourceBarrier {};
            resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            resourceBarrier.UAV.pResource = d3d12Buffer->bufferResource.Get();
            resourceBarriers.push_back(resourceBarrier);
        }
    }

    if (resourceBarriers.size() > 0) {
        m_commandList->ResourceBarrier(narrow_cast<UINT>(resourceBarriers.size()), resourceBarriers.data());
    }
}

D3D12_RESOURCE_BARRIER D3D12CommandList::createResourceTransitionBarrier(D3D12Buffer const& d3d12Buffer, D3D12_RESOURCE_STATES targetResourceState) const
{
    ARKOSE_ASSERT(d3d12Buffer.resourceState != targetResourceState);

    D3D12_RESOURCE_BARRIER resourceBarrier {};
    resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrier.Transition.pResource = d3d12Buffer.bufferResource.Get();
    resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    resourceBarrier.Transition.StateBefore = d3d12Buffer.resourceState;
    resourceBarrier.Transition.StateAfter = targetResourceState;

    d3d12Buffer.resourceState = targetResourceState;
    return resourceBarrier;
}

D3D12_RESOURCE_BARRIER D3D12CommandList::createResourceTransitionBarrier(D3D12Texture const& d3d12Texture, D3D12_RESOURCE_STATES targetResourceState) const
{
    ARKOSE_ASSERT(d3d12Texture.resourceState != targetResourceState);

    D3D12_RESOURCE_BARRIER resourceBarrier {};
    resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resourceBarrier.Transition.pResource = d3d12Texture.textureResource.Get();
    resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    resourceBarrier.Transition.StateBefore = d3d12Texture.resourceState;
    resourceBarrier.Transition.StateAfter = targetResourceState;

    d3d12Texture.resourceState = targetResourceState;
    return resourceBarrier;
}

void D3D12CommandList::createTransitionBarriersForAllReferencedResources(StateBindings const& stateBindings, std::vector<D3D12_RESOURCE_BARRIER>& outBarriers) const
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    stateBindings.forEachBinding([&](ShaderBinding const& bindingInfo) {
        if (bindingInfo.type() == ShaderBindingType::SampledTexture) {
            for (Texture const* texture : bindingInfo.getSampledTextures()) {
                auto& d3d12Texture = static_cast<D3D12Texture const&>(*texture);

                constexpr D3D12_RESOURCE_STATES targetResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                if (d3d12Texture.resourceState != targetResourceState) {
                    D3D12_RESOURCE_BARRIER barrier = createResourceTransitionBarrier(d3d12Texture, targetResourceState);
                    outBarriers.push_back(barrier);
                    d3d12Texture.resourceState = targetResourceState;
                }
            }
        } else if (bindingInfo.type() == ShaderBindingType::StorageTexture) {
            for (TextureMipView textureMip : bindingInfo.getStorageTextures()) {
                auto& d3d12Texture = static_cast<D3D12Texture const&>(textureMip.texture());

                constexpr D3D12_RESOURCE_STATES targetResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                if (d3d12Texture.resourceState != targetResourceState) {
                    D3D12_RESOURCE_BARRIER barrier = createResourceTransitionBarrier(d3d12Texture, targetResourceState);
                    outBarriers.push_back(barrier);
                    d3d12Texture.resourceState = targetResourceState;
                }
            }
        } else if (bindingInfo.type() == ShaderBindingType::StorageBuffer) {
            for (Buffer* storageBuffer : bindingInfo.getBuffers()) {
                auto& d3d12Buffer = static_cast<D3D12Buffer const&>(*storageBuffer);

                constexpr D3D12_RESOURCE_STATES targetResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                if (d3d12Buffer.resourceState != targetResourceState) {
                    D3D12_RESOURCE_BARRIER barrier = createResourceTransitionBarrier(d3d12Buffer, targetResourceState);
                    outBarriers.push_back(barrier);
                    d3d12Buffer.resourceState = targetResourceState;
                }
            }
        } else if (bindingInfo.type() == ShaderBindingType::ConstantBuffer) {
            auto& d3d12Buffer = static_cast<D3D12Buffer const&>(bindingInfo.getBuffer());

            constexpr D3D12_RESOURCE_STATES targetResourceState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            if (d3d12Buffer.resourceState != targetResourceState) {
                D3D12_RESOURCE_BARRIER barrier = createResourceTransitionBarrier(d3d12Buffer, targetResourceState);
                outBarriers.push_back(barrier);
                d3d12Buffer.resourceState = targetResourceState;
            }
        }
    });
}
