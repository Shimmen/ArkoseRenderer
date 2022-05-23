#pragma once

#include "backend/base/RenderTarget.h"

struct D3D12RenderTarget final : public RenderTarget {
public:
    D3D12RenderTarget(Backend&, std::vector<Attachment> attachments);
    virtual ~D3D12RenderTarget() override;

    virtual void setName(const std::string& name) override;
};
