#include "ShadowMapNode.h"

#include "ShadowData.h"

std::string ShadowMapNode::name()
{
    return "shadow";
}

ShadowMapNode::ShadowMapNode(Scene& scene)
    : RenderGraphNode(ShadowMapNode::name())
    , m_scene(scene)
{
}

RenderGraphNode::ExecuteCallback ShadowMapNode::constructFrame(Registry& reg) const
{
    // TODO: This should be managed from some central location, e.g. the scene node or similar.
    Buffer& transformDataBuffer = reg.createBuffer(m_scene.meshCount() * sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& transformBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &transformDataBuffer } });

    Shader shadowMapShader = Shader::createVertexOnly("shadow/defaultShadowMap.vert");

    return [&, shadowMapShader](const AppState& appState, CommandList& cmdList) {

        // TODO: This should be managed from some central location, e.g. the scene node or similar.
        mat4 objectTransforms[SHADOW_MAX_OCCLUDERS];
        int meshCount = m_scene.forEachMesh([&](size_t idx, Mesh& mesh) {
            objectTransforms[idx] = mesh.transform().worldMatrix();
            mesh.ensureVertexBuffer({ VertexComponent::Position3F });
            mesh.ensureIndexBuffer();
        });
        transformDataBuffer.updateData(objectTransforms, meshCount * sizeof(mat4));

        m_scene.forEachMesh([&](size_t idx, Mesh& mesh) {
            mesh.ensureVertexBuffer({ VertexComponent::Position3F });
            mesh.ensureIndexBuffer();
        });

        m_scene.forEachLight([&](size_t, Light& light) {

            if (!light.castsShadows())
                return;

            // TODO: Use a proper cache instead of just using a name as a "cache identifier". This will require implementing operator== on a lot of
            // objects though, which I have barely done at all, so this is a very simple and quick hack to get around that.
            RenderState& renderState = light.getOrCreateCachedShadowMapRenderState("ShadowMapNode::defaultShadowMapping", [&](Registry& sceneRegistry) -> RenderState& {
                RenderStateBuilder renderStateBuilder { light.shadowMapRenderTarget(), shadowMapShader, VertexLayout::positionOnly() };
                renderStateBuilder.addBindingSet(transformBindingSet);
                return sceneRegistry.createRenderState(renderStateBuilder);
            });

            constexpr float clearDepth = 1.0f;
            cmdList.beginRendering(renderState, ClearColor(0, 0, 0), clearDepth);
            {
                cmdList.bindSet(transformBindingSet, 0);

                mat4 lightProjectionFromWorld = light.viewProjection();
                cmdList.setNamedUniform("lightProjectionFromWorld", lightProjectionFromWorld);

                // TODO: Implement frustum culling! Maybe wait until we do that on the GPU for forward with indirect etc.
                m_scene.forEachMesh([&](size_t idx, Mesh& mesh) {
                    cmdList.drawIndexed(mesh.vertexBuffer({ VertexComponent::Position3F }), mesh.indexBuffer(), mesh.indexCount(), mesh.indexType(), idx);
                });
            }
            cmdList.endRendering();
        });
    };
}
