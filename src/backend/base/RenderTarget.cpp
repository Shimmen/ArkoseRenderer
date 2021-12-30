#include "backend/base/RenderTarget.h"

#include "utility/Logging.h"

RenderTarget::RenderTarget(Backend& backend, std::vector<Attachment> attachments)
    : Resource(backend)
{
    // TODO: This is all very messy and could probably be cleaned up a fair bit!

    for (const Attachment& attachment : attachments) {
        if (attachment.type == AttachmentType::Depth) {
            ASSERT(!m_depthAttachment.has_value());
            m_depthAttachment = attachment;
        } else {
            m_colorAttachments.push_back(attachment);
        }
    }

    if (totalAttachmentCount() < 1) {
        LogErrorAndExit("RenderTarget error: tried to create with less than one attachments!\n");
    }

    for (auto& colorAttachment : m_colorAttachments) {
        if (!colorAttachment.texture->isMultisampled() && colorAttachment.multisampleResolveTexture != nullptr)
            LogErrorAndExit("RenderTarget error: tried to create render target with texture that isn't multisampled but has a resolve texture\n");
        if (colorAttachment.texture->isMultisampled() && colorAttachment.multisampleResolveTexture == nullptr)
            LogErrorAndExit("RenderTarget error: tried to create render target with multisample texture but no resolve texture\n");
    }

    Extent2D firstExtent = m_depthAttachment.has_value()
        ? m_depthAttachment.value().texture->extent()
        : m_colorAttachments.front().texture->extent();
    Texture::Multisampling firstMultisampling = m_depthAttachment.has_value()
        ? m_depthAttachment.value().texture->multisampling()
        : m_colorAttachments.front().texture->multisampling();

    for (auto& attachment : m_colorAttachments) {
        if (attachment.texture->extent() != firstExtent) {
            LogErrorAndExit("RenderTarget error: tried to create with attachments of different sizes: (%ix%i) vs (%ix%i)\n",
                            attachment.texture->extent().width(), attachment.texture->extent().height(),
                            firstExtent.width(), firstExtent.height());
        }
        if (attachment.texture->multisampling() != firstMultisampling) {
            LogErrorAndExit("RenderTarget error: tried to create with attachments of different multisampling sample counts: %u vs %u\n",
                            static_cast<unsigned>(attachment.texture->multisampling()), static_cast<unsigned>(firstMultisampling));
        }
    }

    m_extent = firstExtent;
    m_multisampling = firstMultisampling;

    if (colorAttachmentCount() == 0) {
        return;
    }

    // Keep color attachments sorted from Color0, Color1, .. ColorN
    std::sort(m_colorAttachments.begin(), m_colorAttachments.end(), [](const Attachment& left, const Attachment& right) {
        return left.type < right.type;
    });

    // Make sure we don't have duplicated attachment types & that the color attachments aren't sparse
    if (m_colorAttachments[0].type != AttachmentType::Color0)
        LogErrorAndExit("RenderTarget error: sparse color attachments in render target\n");
    auto lastType = AttachmentType::Color0;
    for (size_t i = 1; i < m_colorAttachments.size(); ++i) {
        const Attachment& attachment = m_colorAttachments[i];
        if (attachment.type == lastType)
            LogErrorAndExit("RenderTarget error: duplicate attachment types in render target\n");
        if (static_cast<unsigned>(attachment.type) != static_cast<unsigned>(lastType) + 1)
            LogErrorAndExit("RenderTarget error: sparse color attachments in render target\n");
        lastType = attachment.type;
    }
}

const Extent2D& RenderTarget::extent() const
{
    return m_extent;
}

size_t RenderTarget::colorAttachmentCount() const
{
    return m_colorAttachments.size();
}

size_t RenderTarget::totalAttachmentCount() const
{
    size_t total = colorAttachmentCount();
    if (hasDepthAttachment())
        total += 1;
    return total;
}

bool RenderTarget::hasDepthAttachment() const
{
    return m_depthAttachment.has_value();
}

const std::optional<RenderTarget::Attachment>& RenderTarget::depthAttachment() const
{
    return m_depthAttachment;
}

const std::vector<RenderTarget::Attachment>& RenderTarget::colorAttachments() const
{
    return m_colorAttachments;
}

Texture* RenderTarget::attachment(AttachmentType requestedType) const
{
    if (requestedType == AttachmentType::Depth) {
        if (m_depthAttachment.has_value())
            return m_depthAttachment.value().texture;
        return nullptr;
    }

    for (const RenderTarget::Attachment& attachment : m_colorAttachments) {
        if (attachment.type == requestedType)
            return attachment.texture;
    }

    return nullptr;
}

void RenderTarget::forEachAttachmentInOrder(std::function<void(const Attachment&)> callback) const
{
    for (auto& colorAttachment : m_colorAttachments) {
        callback(colorAttachment);
    }
    if (hasDepthAttachment())
        callback(depthAttachment().value());
}

bool RenderTarget::requiresMultisampling() const
{
    return m_multisampling != Texture::Multisampling::None;
}

Texture::Multisampling RenderTarget::multisampling() const
{
    return m_multisampling;
}
