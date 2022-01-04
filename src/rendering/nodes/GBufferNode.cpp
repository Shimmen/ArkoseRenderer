#include "GBufferNode.h"

#include "utility/Profiling.h"

GBufferNode::GBufferNode(const Scene&)
{
}

RenderPipelineNode::ExecuteCallback GBufferNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    const RenderTarget& windowTarget = reg.windowRenderTarget();

    Texture& normalTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA16F);
    reg.publish("SceneNormal", normalTexture);
    Texture& baseColorTexture = reg.createTexture2D(windowTarget.extent(), Texture::Format::RGBA8);
    reg.publish("SceneBaseColor", baseColorTexture);

    return RenderPipelineNode::NullExecuteCallback;
}
