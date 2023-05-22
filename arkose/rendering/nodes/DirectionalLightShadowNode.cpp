#include "DirectionalLightShadowNode.h"

#include "core/math/Frustum.h"
#include "rendering/GpuScene.h"
#include "scene/lights/Light.h"
#include "rendering/util/ScopedDebugZone.h"
#include "utility/Profiling.h"
#include <imgui.h>

void DirectionalLightShadowNode::drawGui()
{
    ImGui::SliderFloat("Light disc radius", &m_lightDiscRadius, 0.0f, 5.0f);
}

RenderPipelineNode::ExecuteCallback DirectionalLightShadowNode::construct(GpuScene& scene, Registry& reg)
{
    //
    // NOTE: We shouldn't rely on TAA to clean up the noise produced by this as the noise messes with history samples.
    // We should ensure we denoise it before we pass it on, and let TAA just smooth out the last little bit.
    //

    // TODO: Figure out a more robust way of figuring out if we have written depth as required
    //if (!(reg.hasPreviousNode("Prepass") || reg.hasPreviousNode("Forward"))) {
    //    ARKOSE_LOG(Fatal, "Directional light shadow needs scene depth information, can't progress");
    //}

    Texture& sceneDepth = *reg.getTexture("SceneDepth");
    Buffer& cameraDataBuffer = *reg.getBuffer("SceneCameraData");
    Texture& blueNoiseTexArray = *reg.getTexture("BlueNoise");

    Texture& projectedShadowTex = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R8);
    reg.publish("DirectionalLightProjectedShadow", projectedShadowTex);

    Texture& shadowMap = reg.createTexture2D({ 4096, 4096 },
                                             Texture::Format::Depth32F,
                                             Texture::Filters::linear(),
                                             Texture::Mipmap::None,
                                             ImageWrapModes::clampAllToEdge());
    shadowMap.setName("DirectionalLightShadowMap");
    RenderTarget& shadowMapRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &shadowMap } });

    BindingSet& sceneObjectBindingSet = *reg.getBindingSet("SceneObjectSet");

    Shader shadowMapShader = Shader::createVertexOnly("shadow/biasedShadowMap.vert");
    RenderStateBuilder renderStateBuilder { shadowMapRenderTarget, shadowMapShader, m_vertexLayout };
    renderStateBuilder.stateBindings().at(0, sceneObjectBindingSet);
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

        scene.ensureDrawCallIsAvailableForAll(m_vertexLayout);

        {
            ScopedDebugZone zone { cmdList, "Shadow Map Drawing" };

            cmdList.beginRendering(renderState, ClearValue::blackAtMaxDepth());

            cmdList.setNamedUniform<mat4>("lightProjectionFromWorld", lightProjectionFromWorld);
            cmdList.setNamedUniform<vec3>("worldLightDirection", light->forwardDirection());
            cmdList.setNamedUniform<float>("constantBias", light->constantBias(shadowMap.extent()));
            cmdList.setNamedUniform<float>("slopeBias", light->slopeBias(shadowMap.extent()));

            cmdList.bindVertexBuffer(scene.globalVertexBufferForLayout(m_vertexLayout));
            cmdList.bindIndexBuffer(scene.globalIndexBuffer(), scene.globalIndexBufferType());

            for (auto& instance : scene.staticMeshInstances()) {
                if (const StaticMesh* staticMesh = scene.staticMeshForInstance(*instance)) {

                    // TODO: Pick LOD properly
                    const StaticMeshLOD& lod = staticMesh->lodAtIndex(0);

                    ark::aabb3 aabb = staticMesh->boundingBox().transformed(instance->transform().worldMatrix());
                    if (lightFrustum.includesAABB(aabb)) {

                        for (u32 segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
                            StaticMeshSegment const& meshSegment = lod.meshSegments[segmentIdx];

                            // Don't render translucent objects. We still do masked though and pretend they are opaque. This may fail
                            // in some cases but in general if the masked features are small enough it's not really noticable.
                            if (meshSegment.blendMode == BlendMode::Translucent) {
                                continue;
                            }

                            DrawCallDescription drawCall = meshSegment.drawCallDescription(m_vertexLayout, scene);
                            drawCall.firstInstance = instance->drawableHandleForSegmentIndex(segmentIdx).indexOfType<u32>(); // TODO: Put this in some buffer instead!

                            cmdList.issueDrawCall(drawCall);
                        }
                    }
                }
            }

            cmdList.endRendering();
        }

        {
            ScopedDebugZone zone { cmdList, "Shadow Map Projection" };

            vec2 radiusInShadowMapUVs = m_lightDiscRadius * shadowMap.extent().inverse();

            cmdList.setComputeState(shadowProjectionState);
            cmdList.bindSet(shadowProjectionBindingSet, 0);
            cmdList.setNamedUniform<mat4>("lightProjectionFromView", lightProjectionFromView);
            cmdList.setNamedUniform<vec2>("lightDiscRadiusInShadowMapUVs", radiusInShadowMapUVs);
            cmdList.setNamedUniform<int>("frameIndexMod8", appState.frameIndex() % 8);
            cmdList.dispatch(projectedShadowTex.extent3D(), { 16, 16, 1 });
        }
    };
}
