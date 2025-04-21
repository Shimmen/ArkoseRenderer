#include "MeshletDepthOnlyRenderNode.h"

RenderTarget& MeshletDepthOnlyRenderNode::makeRenderTarget(Registry& reg, LoadOp loadOp) const
{
    Texture& depthTexture = *reg.getTexture("SceneDepth");
    return reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &depthTexture, loadOp, StoreOp::Store } });
}

Shader MeshletDepthOnlyRenderNode::makeShader(BlendMode blendMode, std::vector<ShaderDefine> const& shaderDefines) const
{
    auto shaderDefinesCopy = shaderDefines;
    shaderDefinesCopy.push_back(ShaderDefine::makeSymbol("VISBUF_DEPTH_ONLY"));

    if (blendMode == BlendMode::Opaque) {
        return Shader::createMeshShading("meshlet/meshletVisibilityBuffer.task",
                                         "meshlet/meshletVisibilityBuffer.mesh",
                                         shaderDefinesCopy);
    } else {
        return Shader::createMeshShading("meshlet/meshletVisibilityBuffer.task",
                                         "meshlet/meshletVisibilityBuffer.mesh",
                                         "meshlet/meshletVisibilityBuffer.frag",
                                         shaderDefinesCopy);
    }
}
