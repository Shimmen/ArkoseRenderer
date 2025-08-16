#include "DirectionalShadowProjectNode.h"

#include "core/math/Frustum.h"
#include "core/parallel/ParallelFor.h"
#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include "scene/lights/Light.h"
#include "rendering/util/ScopedDebugZone.h"
#include "utility/Profiling.h"
#include <imgui.h>

void DirectionalShadowProjectNode::drawGui()
{
    ImGui::SliderFloat("Light disc radius", &m_lightDiscRadius, 0.0f, 5.0f);
    drawTextureVisualizeGui(*m_shadowMask);
}

RenderPipelineNode::ExecuteCallback DirectionalShadowProjectNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& shadowMap = *reg.getTexture("DirectionalLightShadowMap");
    m_shadowMask = reg.getTexture("DirectionalLightShadowMask");

    //
    // NOTE: We shouldn't rely on TAA to clean up the noise produced by this as the noise messes with history samples.
    // We should ensure we denoise it before we pass it on, and let TAA just smooth out the last little bit.
    //

    Texture& sceneDepth = *reg.getTexture("SceneDepth");
    Buffer& cameraDataBuffer = *reg.getBuffer("SceneCameraData");
    Texture& blueNoiseTexArray = *reg.getTexture("BlueNoise");

    Shader shadowProjectionShader = Shader::createCompute("shadow/projectShadow.comp");
    BindingSet& shadowProjectionBindingSet = reg.createBindingSet({ ShaderBinding::storageTexture(*m_shadowMask, ShaderStage::Compute),
                                                                    ShaderBinding::sampledTexture(shadowMap, ShaderStage::Compute),
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
        mat4 lightProjectionFromView = lightProjectionFromWorld * inverse(scene.camera().viewMatrix());

        vec2 radiusInShadowMapUVs = m_lightDiscRadius * shadowMap.extent().inverse();

        cmdList.setComputeState(shadowProjectionState);
        cmdList.setNamedUniform<mat4>("lightProjectionFromView", lightProjectionFromView);
        cmdList.setNamedUniform<vec2>("lightDiscRadiusInShadowMapUVs", radiusInShadowMapUVs);
        cmdList.setNamedUniform<int>("frameIndexMod8", appState.frameIndex() % 8);
        cmdList.dispatch(m_shadowMask->extent3D(), { 16, 16, 1 });
    };
}
