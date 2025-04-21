#include "DirectionalShadowDrawNode.h"

#include "rendering/GpuScene.h"
#include "scene/lights/DirectionalLight.h"
#include <imgui.h>

void DirectionalShadowDrawNode::drawGui()
{
    drawTextureVisualizeGui(*m_shadowMap);
}

RenderPipelineNode::ExecuteCallback DirectionalShadowDrawNode::construct(GpuScene& scene, Registry& reg)
{
    m_shadowMap = &reg.createTexture2D({ 8192, 8192 },
                                       Texture::Format::Depth32F,
                                       Texture::Filters::linear(),
                                       Texture::Mipmap::None,
                                       ImageWrapModes::clampAllToEdge());
    reg.publish("DirectionalLightShadowMap", *m_shadowMap);

    return MeshletDepthOnlyRenderNode::construct(scene, reg);
}

vec2 DirectionalShadowDrawNode::depthBiasParameters(GpuScene& scene) const
{
    DirectionalLight* light = scene.scene().firstDirectionalLight();
    return { light->constantBias(), light->slopeBias() };
}

mat4 DirectionalShadowDrawNode::calculateViewProjectionMatrix(GpuScene& scene) const
{
    DirectionalLight* light = scene.scene().firstDirectionalLight();
    return light->viewProjection();
}

geometry::Frustum DirectionalShadowDrawNode::calculateCullingFrustum(GpuScene& scene) const
{
    return geometry::Frustum::createFromProjectionMatrix(calculateViewProjectionMatrix(scene));
}

RenderTarget& DirectionalShadowDrawNode::makeRenderTarget(Registry& reg, LoadOp loadOp) const
{
    return reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, m_shadowMap, loadOp, StoreOp::Store } });
}
