#include "ShadowMapNode.h"

#include "math/Frustum.h"
#include "utility/Profiling.h"
#include "ShadowData.h"

ShadowMapNode::ShadowMapNode(Scene& scene)
    : m_scene(scene)
{
}

RenderPipelineNode::ExecuteCallback ShadowMapNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    // TODO: This should be managed from some central location, e.g. the scene node or similar.
    Buffer& transformDataBuffer = reg.createBuffer(m_scene.meshCount() * sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& transformBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &transformDataBuffer } });

    Shader shadowMapShader = Shader::createVertexOnly("shadow/biasedShadowMap.vert");
    BindingSet& shadowDataBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, reg.getBuffer("SceneShadowData") } });

    // HACK: Well, this really is a hack, together with the whole render state cache..
    m_scene.forEachShadowCastingLight([&](size_t shadowLightIndex, Light& light) {
        light.invalidateRenderStateCache();
    });

    return [&, shadowMapShader](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        // TODO: This should be managed from some central location, e.g. the scene node or similar.
        mat4 objectTransforms[SHADOW_MAX_OCCLUDERS];
        int meshCount = m_scene.forEachMesh([&](size_t idx, Mesh& mesh) {
            objectTransforms[idx] = mesh.transform().worldMatrix();
            mesh.ensureDrawCallIsAvailable(m_vertexLayout, m_scene);
        });
        transformDataBuffer.updateData(objectTransforms, meshCount * sizeof(mat4));

        m_scene.forEachShadowCastingLight([&](size_t shadowLightIndex, Light& light) {
            SCOPED_PROFILE_ZONE_NAMED("Processing light");

            // TODO: Use a proper cache instead of just using a name as a "cache identifier". This will require implementing operator== on a lot of
            // objects though, which I have barely done at all, so this is a very simple and quick hack to get around that.
            RenderState& renderState = light.getOrCreateCachedShadowMapRenderState("ShadowMapNode::defaultShadowMapping", [&](Registry& sceneRegistry) -> RenderState& {
                RenderStateBuilder renderStateBuilder { light.shadowMapRenderTarget(), shadowMapShader, m_vertexLayout };
                renderStateBuilder.stateBindings().disableAutoBinding();
                renderStateBuilder.stateBindings().at(0, transformBindingSet);
                renderStateBuilder.stateBindings().at(1, shadowDataBindingSet);
                return sceneRegistry.createRenderState(renderStateBuilder);
            });

            constexpr float clearDepth = 1.0f;
            cmdList.beginRendering(renderState, ClearColor::srgbColor(0, 0, 0), clearDepth);
            {
                mat4 lightProjectionFromWorld = light.viewProjection();
                auto lightFrustum = geometry::Frustum::createFromProjectionMatrix(lightProjectionFromWorld);

                // HACK: Since we only cache one render state per light we can't use the auto-binding, because then we only have the right sets 1/n times.
                // Yeah.. this is a horrible hack. I need to figure out how we want to do this for real. Maybe just manage all the render states here in
                // this node and possibly add some local caching here. This will at least let us manage the different frame contexts etc.
                cmdList.bindSet(transformBindingSet, 0);
                cmdList.bindSet(shadowDataBindingSet, 1);

                uint32_t index = (uint32_t)shadowLightIndex;
                cmdList.setNamedUniform("lightIndex", index);

                cmdList.bindVertexBuffer(m_scene.globalVertexBufferForLayout(m_vertexLayout));
                cmdList.bindIndexBuffer(m_scene.globalIndexBuffer(), m_scene.globalIndexBufferType());

                m_scene.forEachMesh([&](size_t idx, Mesh& mesh) {
                    // Don't render translucent objects. We still do masked though and pretend they are opaque. This may fail
                    // in some cases but in general if the masked features are small enough it's not really noticable.
                    if (mesh.material().blendMode == Material::BlendMode::Translucent)
                        return;

                    geometry::Sphere sphere = mesh.boundingSphere().transformed(mesh.transform().worldMatrix());
                    if (!lightFrustum.includesSphere(sphere))
                        return;

                    DrawCallDescription drawCall = mesh.drawCallDescription(m_vertexLayout, m_scene);
                    drawCall.firstInstance = static_cast<uint32_t>(idx); // TODO: Put this in some buffer instead!

                    cmdList.issueDrawCall(drawCall);
                });
            }
            cmdList.endRendering();
        });
    };
}
