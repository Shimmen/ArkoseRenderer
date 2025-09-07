#include "RTLocalShadowNode.h"

#include "core/math/Frustum.h"
#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include "rendering/util/ScopedDebugZone.h"
#include "scene/lights/Light.h"
#include "utility/Profiling.h"
#include <imgui.h>

void RTLocalShadowNode::drawGui()
{
    m_scene->forEachLocalRTShadow([this](size_t lightIdx, Light const& light, Texture& shadowMaskTex) {
        ImGui::Text("%s", light.name().c_str());
        drawTextureVisualizeGui(shadowMaskTex);
        ImGui::Separator();
    });
}

RenderPipelineNode::ExecuteCallback RTLocalShadowNode::construct(GpuScene& scene, Registry& reg)
{
    m_scene = &scene;

    Texture& sceneDepth = *reg.getTexture("SceneDepth");
    Buffer& cameraDataBuffer = *reg.getBuffer("SceneCameraData");
    // Texture& blueNoiseTexArray = *reg.getTexture("BlueNoise");

    m_shadowTex = &reg.createTexture2D(pipeline().renderResolution(), Texture::Format::R8);
    reg.publish("LocalLightShadow", *m_shadowTex);

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& bindingSet = reg.createBindingSet({ ShaderBinding::topLevelAccelerationStructure(sceneTLAS, ShaderStage::RTRayGen),
                                                    ShaderBinding::constantBuffer(cameraDataBuffer, ShaderStage::RTRayGen),
                                                    ShaderBinding::sampledTexture(sceneDepth, ShaderStage::RTRayGen),
                                                    ShaderBinding::storageTexture(*m_shadowTex, ShaderStage::RTRayGen) });

    ShaderFile raygen { "rt-shadow/raygen.rgen" };
    ShaderFile shadowMissShader { "rayTracing/common/shadow.rmiss" };
    ShaderBindingTable sbt { raygen, { /* no hit groups */ }, { shadowMissShader } };

    StateBindings stateDataBindings;
    stateDataBindings.at(0, bindingSet);

    constexpr uint32_t maxRecursionDepth = 1; // raygen -> shadow ray
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        cmdList.clearTexture(*m_shadowTex, ClearValue::blackAtMaxDepth());

        // geometry::Frustum const& cameraFrustum = scene.camera().frustum();

        scene.forEachLocalRTShadow([&](size_t lightIdx, Light const& light, Texture& shadowMaskTex) {

            // TODO: Define a radius for the light falloff!
            // if (!light.castLightsIntoFrustum(cameraFrustum)) {
            //    return;
            //}

            // TODO: Make this a little nicer.
            ARKOSE_ASSERTM(light.type() == Light::Type::SpotLight, "Expected a SpotLight for local shadows (for now).");
            SpotLight const& spotLight = static_cast<SpotLight const&>(light);

            ScopedDebugZone zone { cmdList, "Local Light" };

            cmdList.setRayTracingState(rtState);

            vec3 lightPosition = light.transform().positionInWorld();
            u32 frameIdxModulus = appState.frameIndex() % 8;

            cmdList.setNamedUniform("parameter1", lightPosition.x);
            cmdList.setNamedUniform("parameter2", lightPosition.y);
            cmdList.setNamedUniform("parameter3", lightPosition.z);
            cmdList.setNamedUniform("parameter4", spotLight.lightSourceRadius());
            cmdList.setNamedUniform("parameter5", static_cast<f32>(frameIdxModulus));

            // TODO: Limit to the radius/influence of the light source onto the world
            cmdList.traceRays(appState.windowExtent());

            // Denoise

            // For now, just copy the shadow mask over
            cmdList.copyTexture(*m_shadowTex, shadowMaskTex);
            //shadowMaskTex.ensureThisTextureIsAvailableToSampleInAShader() // TODO!
        });
    };
}
