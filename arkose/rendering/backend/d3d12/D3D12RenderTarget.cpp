#include "D3D12RenderTarget.h"

#include "rendering/backend/d3d12/D3D12Backend.h"
#include "rendering/backend/d3d12/D3D12Texture.h"
#include "utility/EnumHelpers.h"
#include "utility/Profiling.h"

D3D12RenderTarget::D3D12RenderTarget(Backend& backend, std::vector<Attachment> attachments)
    : RenderTarget(backend, std::move(attachments))
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    auto& d3d12Backend = static_cast<D3D12Backend&>(backend);

    // Create a heap that can contains all the render target descriptors needed
    {
        D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
        descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        descriptorHeapDesc.NumDescriptors = narrow_cast<u32>(colorAttachmentCount());
        descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        descriptorHeapDesc.NodeMask = 0;

        if (auto hr = d3d12Backend.device().CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&renderTargetDescriptorHeap)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12RenderTarget: failed to create render target view descriptor heap for render target, exiting.");
        }
    }

    if (hasDepthAttachment()) {
        D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
        descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        descriptorHeapDesc.NumDescriptors = 1;
        descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        descriptorHeapDesc.NodeMask = 0;

        if (auto hr = d3d12Backend.device().CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&depthStencilDescriptorHeap)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12RenderTarget: failed to create depth stencil view descriptor heap for render target, exiting.");
        }
    }

    // Reset to zeroes so you can see which are unused when debugging
    std::memset(&depthStencilRenderTargetHandle, 0, sizeof(depthStencilRenderTargetHandle));
    std::memset(colorRenderTargetHandles, 0, sizeof(colorRenderTargetHandles));

    u32 nextDescriptorIdx = 0;
    for (RenderTarget::Attachment const& colorAttachment : colorAttachments()) {

        D3D12Texture& d3d12ColorTexture = static_cast<D3D12Texture&>(*colorAttachment.texture);

        // Not yet supported!
        ARKOSE_ASSERT(d3d12ColorTexture.isMultisampled() == false);

        D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc {};

        renderTargetViewDesc.Format = d3d12ColorTexture.dxgiFormat;

        ARKOSE_ASSERT(d3d12ColorTexture.description().type == Texture::Type::Texture2D);
        ARKOSE_ASSERT(d3d12ColorTexture.extent3D().depth() == 1);
        ARKOSE_ASSERT(d3d12ColorTexture.isArray() == false);
        renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        // Always bind mip0 as the first one for render targets
        renderTargetViewDesc.Texture2D.MipSlice = 0;
        // ??
        renderTargetViewDesc.Texture2D.PlaneSlice = 0;

        u32 attachmentIdx = toUnderlying(colorAttachment.type);
        D3D12_CPU_DESCRIPTOR_HANDLE& colorRenderTargetHandle = colorRenderTargetHandles[attachmentIdx];

        static const UINT renderTargetViewDescriptorHandleIncrementSize = d3d12Backend.device().GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE::InitOffsetted(colorRenderTargetHandle, renderTargetDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                                     nextDescriptorIdx++, renderTargetViewDescriptorHandleIncrementSize);

        d3d12Backend.device().CreateRenderTargetView(d3d12ColorTexture.textureResource.Get(),
                                                     &renderTargetViewDesc,
                                                     colorRenderTargetHandle);
    }

    ARKOSE_ASSERT(nextDescriptorIdx == colorAttachmentCount());

    if (hasDepthAttachment()) {
        RenderTarget::Attachment const& depthAttachmentData = depthAttachment().value();
        D3D12Texture& d3d12DepthTexture = static_cast<D3D12Texture&>(*depthAttachmentData.texture);

        // Not yet supported!
        ARKOSE_ASSERT(d3d12DepthTexture.isMultisampled() == false);

        D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc {};

        ARKOSE_ASSERT(d3d12DepthTexture.hasDepthFormat());
        depthStencilViewDesc.Format = d3d12DepthTexture.dxgiFormat;

        ARKOSE_ASSERT(d3d12DepthTexture.description().type == Texture::Type::Texture2D);
        ARKOSE_ASSERT(d3d12DepthTexture.extent3D().depth() == 1);
        ARKOSE_ASSERT(d3d12DepthTexture.isArray() == false);
        depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        // TODO: Consider if/when we should set
        //   D3D12_DSV_FLAG_READ_ONLY_DEPTH or
        //   D3D12_DSV_FLAG_READ_ONLY_STENCIL
        depthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;

        // Always bind mip0 as the first one for render targets
        depthStencilViewDesc.Texture2D.MipSlice = 0;

        static const UINT depthStencilViewDescriptorHandleIncrementSize = d3d12Backend.device().GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE::InitOffsetted(depthStencilRenderTargetHandle, depthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                                     0, depthStencilViewDescriptorHandleIncrementSize);

        d3d12Backend.device().CreateDepthStencilView(d3d12DepthTexture.textureResource.Get(),
                                                     &depthStencilViewDesc,
                                                     depthStencilRenderTargetHandle);
    }
}

D3D12RenderTarget::~D3D12RenderTarget()
{
    if (!hasBackend())
        return;
    // TODO
}

void D3D12RenderTarget::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    // TODO
}
