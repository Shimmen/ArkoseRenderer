#pragma once

#include "rendering/backend/base/RenderTarget.h"

#include "rendering/backend/d3d12/D3D12Common.h"

struct D3D12RenderTarget final : public RenderTarget {
public:
    D3D12RenderTarget(Backend&, std::vector<Attachment> attachments);
    virtual ~D3D12RenderTarget() override;

    virtual void setName(const std::string& name) override;

    ComPtr<ID3D12DescriptorHeap> renderTargetDescriptorHeap;
    ComPtr<ID3D12DescriptorHeap> depthStencilDescriptorHeap;

    // NOTE: Must match the attached textures 1:1
    // NOTE: Mutable to allow for updating in case of rendering to the swapchain (as we don't know the exact handle until we start rendering the frame)
    mutable D3D12_CPU_DESCRIPTOR_HANDLE colorRenderTargetHandles[8];
    D3D12_CPU_DESCRIPTOR_HANDLE depthStencilRenderTargetHandle;
};
