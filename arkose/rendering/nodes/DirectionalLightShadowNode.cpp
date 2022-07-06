#include "DirectionalLightShadowNode.h"

#include "math/Frustum.h"
#include "rendering/scene/GpuScene.h"
#include "rendering/scene/lights/Light.h"
#include "utility/Profiling.h"
#include <imgui.h>

RenderPipelineNode::ExecuteCallback DirectionalLightShadowNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& sceneDepth = *reg.getTexture("SceneDepth");
    Buffer& cameraDataBuffer = *reg.getBuffer("SceneCameraData");
    Texture& blueNoiseTexArray = *reg.getTexture("BlueNoise");

    Texture& projectedShadowTex = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R8);
    reg.publish("DirectionalLightProjectedShadow", projectedShadowTex);

    Texture& shadowMap = reg.createTexture2D({ 4096, 4096 },
                                             Texture::Format::Depth32F,
                                             Texture::Filters::linear(),
                                             Texture::Mipmap::None,
                                             Texture::WrapModes::clampAllToEdge());
    shadowMap.setName("DirectionalLightShadowMap");
    RenderTarget& shadowMapRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &shadowMap } });

    BindingSet& sceneObjectBindingSet = *reg.getBindingSet("SceneObjectSet");
    BindingSet& shadowDataBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(*reg.getBuffer("SceneShadowData"), ShaderStage::Vertex) });

    Shader shadowMapShader = Shader::createVertexOnly("shadow/biasedShadowMap.vert");
    RenderStateBuilder renderStateBuilder { shadowMapRenderTarget, shadowMapShader, m_vertexLayout };
    renderStateBuilder.stateBindings().at(0, sceneObjectBindingSet);
    renderStateBuilder.stateBindings().at(1, shadowDataBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    Shader shadowProjectionShader = Shader::createCompute("shadow/projectShadow.comp");
    BindingSet& shadowProjectionBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(projectedShadowTex, ShaderStage::Compute),
                                                                    ShaderBinding::sampledTexture(shadowMap, ShaderStage::Compute),
                                                                    ShaderBinding::sampledTexture(sceneDepth, ShaderStage::Compute),
                                                                    ShaderBinding::constantBuffer(cameraDataBuffer, ShaderStage::Compute),
                                                                    ShaderBinding::sampledTexture(blueNoiseTexArray, ShaderStage::Compute) });
    ComputeState& shadowProjectionState = reg.createComputeState(shadowProjectionShader, { &shadowProjectionBindingSet });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        DirectionalLight* light = scene.scene().firstDirectionalLight();
        if (light == nullptr || !light->castsShadows()) {
            return;
        }

        mat4 lightProjectionFromWorld = light->viewProjection();
        auto lightFrustum = geometry::Frustum::createFromProjectionMatrix(lightProjectionFromWorld);
        mat4 lightProjectionFromView = lightProjectionFromWorld * inverse(scene.camera().viewMatrix());

        scene.forEachMesh([&](size_t idx, Mesh& mesh) {
            mesh.ensureDrawCallIsAvailable(m_vertexLayout, scene);
        });

        constexpr float clearDepth = 1.0f;
        cmdList.beginRendering(renderState, ClearColor::srgbColor(0, 0, 0), clearDepth);
        {
            uint32_t index = 0; // first directional light is always index 0
            cmdList.setNamedUniform("lightIndex", index);

            cmdList.bindVertexBuffer(scene.globalVertexBufferForLayout(m_vertexLayout));
            cmdList.bindIndexBuffer(scene.globalIndexBuffer(), scene.globalIndexBufferType());

            scene.forEachMesh([&](size_t idx, Mesh& mesh) {
                // Don't render translucent objects. We still do masked though and pretend they are opaque. This may fail
                // in some cases but in general if the masked features are small enough it's not really noticable.
                if (mesh.material().blendMode == Material::BlendMode::Translucent)
                    return;

                geometry::Sphere sphere = mesh.boundingSphere().transformed(mesh.transform().worldMatrix());
                if (!lightFrustum.includesSphere(sphere))
                    return;

                DrawCallDescription drawCall = mesh.drawCallDescription(m_vertexLayout, scene);
                drawCall.firstInstance = static_cast<uint32_t>(idx); // TODO: Put this in some buffer instead!

                cmdList.issueDrawCall(drawCall);
            });
        }
        cmdList.endRendering();

        ImGui::SliderFloat("Light disc radius", &m_lightDiscRadius, 0.0f, 5.0f);
        vec2 radiusInShadowMapUVs = m_lightDiscRadius * shadowMap.extent().inverse();

        cmdList.setComputeState(shadowProjectionState);
        cmdList.bindSet(shadowProjectionBindingSet, 0);
        cmdList.setNamedUniform<mat4>("lightProjectionFromView", lightProjectionFromView);
        cmdList.setNamedUniform<vec2>("lightDiscRadiusInShadowMapUVs", radiusInShadowMapUVs);
        cmdList.setNamedUniform<int>("frameIndexMod8", appState.frameIndex() % 8);
        cmdList.dispatch(projectedShadowTex.extent3D(), { 16, 16, 1 });
    };
}
