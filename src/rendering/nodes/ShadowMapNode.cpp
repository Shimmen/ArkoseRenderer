#include "ShadowMapNode.h"

#include "geometry/Frustum.h"
#include "utility/Profiling.h"
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
    SCOPED_PROFILE_ZONE();

    // TODO: This should be managed from some central location, e.g. the scene node or similar.
    Buffer& transformDataBuffer = reg.createBuffer(m_scene.meshCount() * sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& transformBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &transformDataBuffer } });

    Shader shadowMapShader = Shader::createVertexOnly("shadow/defaultShadowMap.vert");

    return [&, shadowMapShader](const AppState& appState, CommandList& cmdList) {

        // TODO: This should be managed from some central location, e.g. the scene node or similar.
        mat4 objectTransforms[SHADOW_MAX_OCCLUDERS];
        int meshCount = m_scene.forEachMesh([&](size_t idx, Mesh& mesh) {
            objectTransforms[idx] = mesh.transform().worldMatrix();
            mesh.ensureDrawCallIsAvailable({ VertexComponent::Position3F }, m_scene);
        });
        transformDataBuffer.updateData(objectTransforms, meshCount * sizeof(mat4));

        m_scene.forEachLight([&](size_t, Light& light) {
            SCOPED_PROFILE_ZONE_NAMED("Processing light");

            if (!light.castsShadows())
                return;

            // TODO: Use a proper cache instead of just using a name as a "cache identifier". This will require implementing operator== on a lot of
            // objects though, which I have barely done at all, so this is a very simple and quick hack to get around that.
            RenderState& renderState = light.getOrCreateCachedShadowMapRenderState("ShadowMapNode::defaultShadowMapping", [&](Registry& sceneRegistry) -> RenderState& {
                RenderStateBuilder renderStateBuilder { light.shadowMapRenderTarget(), shadowMapShader, VertexLayout { VertexComponent::Position3F } };
                renderStateBuilder.addBindingSet(transformBindingSet);
                return sceneRegistry.createRenderState(renderStateBuilder);
            });

            constexpr float clearDepth = 1.0f;
            cmdList.beginRendering(renderState, ClearColor(0, 0, 0), clearDepth);
            {
                mat4 lightProjectionFromWorld = light.viewProjection();
                auto lightFrustum = geometry::Frustum::createFromProjectionMatrix(lightProjectionFromWorld);

                cmdList.setNamedUniform("lightProjectionFromWorld", lightProjectionFromWorld);
                cmdList.bindSet(transformBindingSet, 0);

                cmdList.bindVertexBuffer(m_scene.globalVertexBufferForLayout({ VertexComponent::Position3F }));
                cmdList.bindIndexBuffer(m_scene.globalIndexBuffer(), m_scene.globalIndexBufferType());

                m_scene.forEachMesh([&](size_t idx, Mesh& mesh) {
                    geometry::Sphere sphere = mesh.boundingSphere().transformed(mesh.transform().worldMatrix());
                    if (!lightFrustum.includesSphere(sphere))
                        return;

                    DrawCallDescription drawCall = mesh.drawCallDescription({ VertexComponent::Position3F }, m_scene);
                    drawCall.firstInstance = static_cast<uint32_t>(idx); // TODO: Put this in some buffer instead!

                    cmdList.issueDrawCall(drawCall);
                });
            }
            cmdList.endRendering();
        });
    };
}
