#include "GBufferNode.h"

#include "utility/Profiling.h"

GBufferNode::GBufferNode(const Scene&)
    : RenderGraphNode(GBufferNode::name())
{
}

std::string GBufferNode::name()
{
    return "g-buffer";
}

RenderGraphNode::ExecuteCallback GBufferNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    const RenderTarget& windowTarget = reg.windowRenderTarget();

    Texture& normalTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA16F);
    normalTexture.setName("GBufferNormal");
    reg.publish("normal", normalTexture);

    Texture& depthTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::Depth24Stencil8, Texture::Filters::nearest());
    depthTexture.setName("GBufferDepth");
    reg.publish("depth", depthTexture);

    Texture& baseColorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8);
    baseColorTexture.setName("GBufferBaseColor");
    reg.publish("baseColor", baseColorTexture);

    return [&](const AppState& appState, CommandList& cmdList) {};
}
