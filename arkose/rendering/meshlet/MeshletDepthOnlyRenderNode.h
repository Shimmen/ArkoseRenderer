#pragma once

#include "rendering/meshlet/MeshletVisibilityBufferRenderNode.h"

class MeshletDepthOnlyRenderNode : public MeshletVisibilityBufferRenderNode {
public:
    std::string name() const override { return "Meshlet depth-only"; }

protected:
    RenderTarget& makeRenderTarget(Registry&, LoadOp loadOp) const override;
    Shader makeShader(BlendMode, std::vector<ShaderDefine> const& shaderDefines) const override;
};
