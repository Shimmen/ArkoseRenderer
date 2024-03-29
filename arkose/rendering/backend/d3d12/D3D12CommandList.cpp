#include "D3D12CommandList.h"

#include "utility/Profiling.h"
#include "rendering/backend/d3d12/D3D12Backend.h"
#include "rendering/backend/d3d12/D3D12Common.h"
#include "rendering/backend/d3d12/D3D12BindingSet.h"
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

void D3D12CommandList::copyTexture(Texture& genSrc, Texture& genDst, uint32_t srcMip, uint32_t dstMip)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::generateMipmaps(Texture& genTexture)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
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
                D3D12_RESOURCE_BARRIER resourceBarrier {};
                resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                resourceBarrier.Transition.pResource = dstBuffer.bufferResource.Get();
                resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                resourceBarrier.Transition.StateBefore = dstBuffer.resourceState;
                resourceBarrier.Transition.StateAfter = expectedResourceState;

                m_commandList->ResourceBarrier(1, &resourceBarrier);
                dstBuffer.resourceState = expectedResourceState;
            }

            m_commandList->CopyBufferRegion(dstBuffer.bufferResource.Get(), copyDestination.offset,
                                            srcBuffer.bufferResource.Get(), copyOperation.srcOffset,
                                            copyOperation.size);

            // TODO: Barrier after?!

        } else if (std::holds_alternative<BufferCopyOperation::TextureDestination>(copyOperation.destination)) {
            auto const& copyDestination = std::get<BufferCopyOperation::TextureDestination>(copyOperation.destination);

            D3D12Texture& dstTexture = *static_cast<D3D12Texture*>(copyDestination.texture);
            ID3D12Resource* srcBuffer = static_cast<D3D12Buffer*>(copyOperation.srcBuffer)->bufferResource.Get();

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
    //m_activeComputeState = nullptr;

    auto& renderTarget = static_cast<const D3D12RenderTarget&>(renderState.renderTarget());

    // Ensure all attached textures are in a suitable state for being rendered to!
    {
        std::vector<D3D12_RESOURCE_BARRIER> resourceBarriers {};

        for (RenderTarget::Attachment const& attachment : renderTarget.colorAttachments()) {
            auto& attachedTexture = static_cast<D3D12Texture&>(*attachment.texture);

            constexpr D3D12_RESOURCE_STATES targetResourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            if (attachedTexture.resourceState != targetResourceState) {

                D3D12_RESOURCE_BARRIER resourceBarrier {};
                resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                resourceBarrier.Transition.pResource = attachedTexture.textureResource.Get();
                resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                resourceBarrier.Transition.StateBefore = attachedTexture.resourceState;
                resourceBarrier.Transition.StateAfter = targetResourceState;

                resourceBarriers.push_back(resourceBarrier);
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

                D3D12_RESOURCE_BARRIER resourceBarrier {};
                resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                resourceBarrier.Transition.pResource = attachedDepthTexture.textureResource.Get();
                resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                resourceBarrier.Transition.StateBefore = attachedDepthTexture.resourceState;
                resourceBarrier.Transition.StateAfter = targetResourceState;

                resourceBarriers.push_back(resourceBarrier);
                attachedDepthTexture.resourceState = targetResourceState;
            }
        }

        if (resourceBarriers.size() > 0) {
            m_commandList->ResourceBarrier(narrow_cast<u32>(resourceBarriers.size()), resourceBarriers.data());
        }
    }

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
    m_commandList->OMSetRenderTargets(renderTarget.colorAttachmentCount(), renderTarget.colorRenderTargetHandles, singleHandleToDescriptorRange,
                                      renderTarget.hasDepthAttachment() ? &renderTarget.depthStencilRenderTargetHandle : nullptr);

    // TODO: Explicitly transition the layouts of the referenced textures to an optimal layout (if it isn't already)
    //renderState.stateBindings().forEachBinding([&](ShaderBinding const& bindingInfo) {
    //    if (bindingInfo.type() == ShaderBindingType::SampledTexture) {
    //        for (Texture const* texture : bindingInfo.getSampledTextures()) {
    //            auto& d3d12Texture = static_cast<D3D12Texture const&>(*texture);
    //
    //            constexpr D3D12_RESOURCE_STATES targetResourceState = ..?;
    //            if (d3d12Texture.resourceState != targetResourceState) {
    //                // ...
    //                d3d12Texture.resourceState = targetResourceState;
    //            }
    //        }
    //    } else if (bindingInfo.type() == ShaderBindingType::StorageTexture) {
    //        for (TextureMipView textureMip : bindingInfo.getStorageTextures()) {
    //            auto& d3d12Texture = static_cast<D3D12Texture const&>(textureMip.texture());
    //
    //            constexpr D3D12_RESOURCE_STATES targetResourceState = ..?;
    //            if (d3d12Texture.resourceState != targetResourceState) {
    //                // ...
    //                d3d12Texture.resourceState = targetResourceState;
    //            }
    //        }
    //    }
    //});

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
}

void D3D12CommandList::endRendering()
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    m_activeRenderState = nullptr;
}

void D3D12CommandList::setRayTracingState(const RayTracingState& rtState)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::setComputeState(const ComputeState& genComputeState)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::evaluateUpscaling(UpscalingState const&, UpscalingParameters)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::bindTextureSet(BindingSet&, u32 index)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::setNamedUniform(const std::string& name, void* data, size_t size)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
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

void D3D12CommandList::bindVertexBuffer(Buffer const& vertexBuffer, u32 stride, u32 bindingIdx)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (vertexBuffer.usage() != Buffer::Usage::Vertex) {
        ARKOSE_LOG(Fatal, "bindVertexBuffer: not a vertex buffer!");
    }

    ID3D12Resource* d3d12BufferResource = static_cast<D3D12Buffer const&>(vertexBuffer).bufferResource.Get();

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    vertexBufferView.BufferLocation = d3d12BufferResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBuffer.size());
    vertexBufferView.StrideInBytes = stride;

    m_commandList->IASetVertexBuffers(bindingIdx, 1, &vertexBufferView);

    m_boundVertexBuffer = d3d12BufferResource;
}

void D3D12CommandList::bindIndexBuffer(Buffer const& indexBuffer, IndexType indexType)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    if (indexBuffer.usage() != Buffer::Usage::Index) {
        ARKOSE_LOG(Fatal, "bindIndexBuffer: not an index buffer!");
    }

    ID3D12Resource* d3d12BufferResource = static_cast<D3D12Buffer const&>(indexBuffer).bufferResource.Get();

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

void D3D12CommandList::textureWriteBarrier(const Texture& genTexture)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::textureMipWriteBarrier(const Texture& genTexture, uint32_t mip)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}

void D3D12CommandList::bufferWriteBarrier(std::vector<Buffer const*> buffers)
{
    SCOPED_PROFILE_ZONE_GPUCOMMAND();

    NOT_YET_IMPLEMENTED();
}
