#pragma once

#include "rendering/backend/Resource.h"
#include "rendering/backend/base/Texture.h"
#include <functional>
#include <optional>

enum class LoadOp {
    Clear,
    Load,
    Discard,
};

enum class StoreOp {
    Discard,
    Store,
};

enum class RenderTargetBlendMode {
    None,
    Additive,
    AlphaBlending,
    PremultipliedAlphaBlending,
};

class RenderTarget : public Resource {
public:

    enum class AttachmentType : u32 {
        Color0 = 0,
        Color1 = 1,
        Color2 = 2,
        Color3 = 3,
        Color4 = 4,
        Color5 = 5,
        Color6 = 6,
        Color7 = 7,
        Depth = UINT_MAX
    };

    // TODO: Create helper functions for creating these, e.g., basicColorAttachment(tex, AttachmentType),
    //  multisampleAttachment(msTex, resolveTex, AttachmentType). This makes is way more readable in the end.
    struct Attachment {
        AttachmentType type { AttachmentType::Color0 };
        Texture* texture { nullptr };
        LoadOp loadOp { LoadOp::Clear };
        StoreOp storeOp { StoreOp::Store };
        RenderTargetBlendMode blendMode { RenderTargetBlendMode::None };
        Texture* multisampleResolveTexture { nullptr };
    };

    RenderTarget() = default;
    RenderTarget(Backend&, std::vector<Attachment> attachments);

    [[nodiscard]] const Extent2D& extent() const;
    [[nodiscard]] size_t colorAttachmentCount() const;
    [[nodiscard]] size_t totalAttachmentCount() const;

    [[nodiscard]] bool hasDepthAttachment() const;
    const std::optional<Attachment>& depthAttachment() const;

    const std::vector<Attachment>& colorAttachments() const;

    [[nodiscard]] Texture* attachment(AttachmentType) const;
    void forEachAttachmentInOrder(std::function<void(const Attachment&)>) const;

    bool requiresMultisampling() const;
    Texture::Multisampling multisampling() const;

private:
    std::vector<Attachment> m_colorAttachments {};
    std::optional<Attachment> m_depthAttachment {};

    Extent2D m_extent;
    Texture::Multisampling m_multisampling;
};
