#include "RTReflectionsNode.h"

#include "rendering/GpuScene.h"
#include <imgui.h>

RenderPipelineNode::ExecuteCallback RTReflectionsNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& reflectionsImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("Reflections", reflectionsImage);

    BindingSet& rtMeshDataBindingSet = *reg.getBindingSet("SceneRTMeshDataSet");
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ ShaderBinding::topLevelAccelerationStructure(sceneTLAS, ShaderStage::RTRayGen | ShaderStage::RTClosestHit),
                                                         ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::AnyRayTrace),
                                                         ShaderBinding::storageTexture(reflectionsImage, ShaderStage::RTRayGen),
                                                         ShaderBinding::sampledTexture(*reg.getTexture("SceneMaterial"), ShaderStage::RTRayGen),
                                                         ShaderBinding::sampledTexture(*reg.getTexture("SceneNormalVelocity"), ShaderStage::RTRayGen),
                                                         ShaderBinding::sampledTexture(*reg.getTexture("SceneDepth"), ShaderStage::RTRayGen),
                                                         ShaderBinding::sampledTexture(scene.environmentMapTexture(), ShaderStage::RTRayGen) });

    ShaderFile raygen { "rt-reflections/raygen.rgen" };
    ShaderFile defaultMissShader { "rayTracing/common/miss.rmiss" };
    ShaderFile shadowMissShader { "rayTracing/common/shadow.rmiss" };
    HitGroup mainHitGroup { ShaderFile("rayTracing/common/opaque.rchit"),
                            ShaderFile("rayTracing/common/masked.rahit") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { defaultMissShader, shadowMissShader } };

    StateBindings stateDataBindings;
    stateDataBindings.at(0, frameBindingSet);
    stateDataBindings.at(1, rtMeshDataBindingSet);
    stateDataBindings.at(2, materialBindingSet);
    stateDataBindings.at(3, lightBindingSet);

    constexpr uint32_t maxRecursionDepth = 2; // raygen -> closest hit -> shadow ray
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        cmdList.clearTexture(reflectionsImage, ClearValue::blackAtMaxDepth());
        cmdList.setRayTracingState(rtState);

        static float injectedAmbient = 500.0f;
        ImGui::SliderFloat("Injected ambient", &injectedAmbient, 0.0f, 1'000.0f);
        cmdList.setNamedUniform("ambientAmount", injectedAmbient * scene.lightPreExposure());

        cmdList.setNamedUniform("environmentMultiplier", scene.preExposedEnvironmentBrightnessFactor());

        static float mirrorRoughnessThreshold = 0.2f;
        static float fullyDiffuseRoughnessThreshold = 0.96f;
        ImGui::SliderFloat("Perfect mirror threshold", &mirrorRoughnessThreshold, 0.0f, fullyDiffuseRoughnessThreshold - 0.01f);
        ImGui::SliderFloat("Fully diffuse threshold", &fullyDiffuseRoughnessThreshold, mirrorRoughnessThreshold + 0.01f, 1.0f);
        cmdList.setNamedUniform("parameter1", mirrorRoughnessThreshold);
        cmdList.setNamedUniform("parameter2", fullyDiffuseRoughnessThreshold);

        cmdList.traceRays(appState.windowExtent());
    };
}
