#include "DirectionalLightShadowNode.h"

#include "core/math/Frustum.h"
#include "core/parallel/ParallelFor.h"
#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include "scene/lights/Light.h"
#include "rendering/util/ScopedDebugZone.h"
#include "utility/Profiling.h"
#include <imgui.h>

void DirectionalLightShadowNode::drawGui()
{
    ImGui::SliderFloat("Light disc radius", &m_lightDiscRadius, 0.0f, 5.0f);
    drawTextureVisualizeGui(*m_shadowMap);
    drawTextureVisualizeGui(*m_projectedShadow);
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

    m_projectedShadow = &reg.createTexture2D(pipeline().renderResolution(), Texture::Format::R8);
    reg.publish("DirectionalLightProjectedShadow", *m_projectedShadow);

    m_shadowMap = &reg.createTexture2D({ 8192, 8192 },
                                       Texture::Format::Depth32F,
                                       Texture::Filters::linear(),
                                       Texture::Mipmap::None,
                                       ImageWrapModes::clampAllToEdge());
    reg.publish("DirectionalLightShadowMap", *m_shadowMap);

    RenderTarget& shadowMapRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, m_shadowMap } });

    BindingSet& sceneObjectBindingSet = *reg.getBindingSet("SceneObjectSet");

    Shader shadowMapShader = Shader::createVertexOnly("shadow/shadowMap.vert");

    RenderStateBuilder renderStateBuilder { shadowMapRenderTarget, shadowMapShader, { scene.vertexManager().positionVertexLayout() } };
    renderStateBuilder.enableDepthBias = true;
    renderStateBuilder.stateBindings().at(0, sceneObjectBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    Shader shadowProjectionShader = Shader::createCompute("shadow/projectShadow.comp");
    BindingSet& shadowProjectionBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(*m_projectedShadow, ShaderStage::Compute),
                                                                    ShaderBinding::sampledTexture(*m_shadowMap, ShaderStage::Compute),
                                                                    ShaderBinding::sampledTexture(sceneDepth, ShaderStage::Compute),
                                                                    ShaderBinding::constantBuffer(cameraDataBuffer, ShaderStage::Compute),
                                                                    ShaderBinding::sampledTexture(blueNoiseTexArray, ShaderStage::Compute) });
    StateBindings projectionStateBindings;
    projectionStateBindings.at(0, shadowProjectionBindingSet);
    ComputeState& shadowProjectionState = reg.createComputeState(shadowProjectionShader, projectionStateBindings);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        DirectionalLight* light = scene.scene().firstDirectionalLight();
        if (light == nullptr || !light->castsShadows()) {
            return;
        }

        mat4 lightProjectionFromWorld = light->viewProjection();
        auto lightFrustum = geometry::Frustum::createFromProjectionMatrix(lightProjectionFromWorld);
        mat4 lightProjectionFromView = lightProjectionFromWorld * inverse(scene.camera().viewMatrix());

        {
            ScopedDebugZone zone { cmdList, "Shadow Map Drawing" };

            cmdList.beginRendering(renderState, ClearValue::blackAtMaxDepth());

            cmdList.setNamedUniform<mat4>("lightProjectionFromWorld", lightProjectionFromWorld);

            cmdList.bindVertexBuffer(scene.vertexManager().positionVertexBuffer(), scene.vertexManager().positionVertexLayout().packedVertexSize(), 0);
            cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

            cmdList.setDepthBias(light->constantBias(), light->slopeBias());

            moodycamel::ConcurrentQueue<DrawCallDescription> drawCalls {};

            auto& instances = scene.staticMeshInstances();
            ParallelForBatched(instances.size(), 256, [&](size_t idx) {
                auto& instance = instances[idx];

                if (const StaticMesh* staticMesh = scene.staticMeshForInstance(*instance)) {

                    if (!staticMesh->hasNonTranslucentSegments()) {
                        return;
                    }

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

                            DrawCallDescription drawCall = meshSegment.vertexAllocation.asDrawCallDescription();
                            drawCall.firstInstance = instance->drawableHandleForSegmentIndex(segmentIdx).indexOfType<u32>(); // TODO: Put this in some buffer instead!

                            drawCalls.enqueue(drawCall);
                        }
                    }
                }
            });

            DrawCallDescription drawCall;
            while (drawCalls.try_dequeue(drawCall)) {
                cmdList.issueDrawCall(drawCall);
            }

            cmdList.endRendering();
        }

        {
            ScopedDebugZone zone { cmdList, "Shadow Map Projection" };

            vec2 radiusInShadowMapUVs = m_lightDiscRadius * m_shadowMap->extent().inverse();

            cmdList.setComputeState(shadowProjectionState);
            cmdList.setNamedUniform<mat4>("lightProjectionFromView", lightProjectionFromView);
            cmdList.setNamedUniform<vec2>("lightDiscRadiusInShadowMapUVs", radiusInShadowMapUVs);
            cmdList.setNamedUniform<int>("frameIndexMod8", appState.frameIndex() % 8);
            cmdList.dispatch(m_projectedShadow->extent3D(), { 16, 16, 1 });
        }
    };
}
