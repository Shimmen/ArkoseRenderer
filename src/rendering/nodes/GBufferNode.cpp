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
    normalTexture.setName("SceneNormal");
    reg.publish("SceneNormal", normalTexture);

    Texture& depthTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::Depth24Stencil8, Texture::Filters::nearest());
    depthTexture.setName("SceneDepth");
    reg.publish("SceneDepth", depthTexture);

    Texture& baseColorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8);
    baseColorTexture.setName("SceneBaseColor");
    reg.publish("SceneBaseColor", baseColorTexture);

    return [&](const AppState& appState, CommandList& cmdList) {};
}
