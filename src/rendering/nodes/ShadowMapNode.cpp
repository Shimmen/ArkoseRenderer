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
    // TODO: Render all applicable shadow maps here, not just the default 'sun' as we do now.
    DirectionalLight& sunLight = m_scene.sun();

    Buffer& lightDataBuffer = reg.createBuffer(sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& lightBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &lightDataBuffer } });

    Buffer& transformDataBuffer = reg.createBuffer(m_scene.meshCount() * sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& transformBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &transformDataBuffer } });

    const RenderTarget& shadowRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &sunLight.shadowMap() } });
    Shader shader = Shader::createVertexOnly("shadow/shadowSun.vert");
    VertexLayout vertexLayout = VertexLayout { sizeof(vec3), { { 0, VertexAttributeType::Float3, 0 } } };

    RenderStateBuilder renderStateBuilder { shadowRenderTarget, shader, vertexLayout };
    renderStateBuilder
        .addBindingSet(lightBindingSet)
        .addBindingSet(transformBindingSet);

    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        mat4 objectTransforms[SHADOW_MAX_OCCLUDERS];
        int meshCount = m_scene.forEachMesh([&](size_t idx, Mesh& mesh) {
            objectTransforms[idx] = mesh.transform().worldMatrix();
            mesh.ensureVertexBuffer({ VertexComponent::Position3F });
            mesh.ensureIndexBuffer();
        });
        transformDataBuffer.updateData(objectTransforms, meshCount * sizeof(mat4));

        mat4 lightProjectionFromWorld = sunLight.viewProjection();
        lightDataBuffer.updateData(&lightProjectionFromWorld, sizeof(mat4));

        cmdList.beginRendering(renderState, ClearColor(1, 0, 1), 1.0f);
        cmdList.bindSet(lightBindingSet, 0);
        cmdList.bindSet(transformBindingSet, 1);

        m_scene.forEachMesh([&](size_t idx, Mesh& mesh) {
            cmdList.drawIndexed(mesh.vertexBuffer({ VertexComponent::Position3F }), mesh.indexBuffer(), mesh.indexCount(), mesh.indexType(), idx);
        });
    };
}
