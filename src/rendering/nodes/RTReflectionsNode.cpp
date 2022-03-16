#include "RTReflectionsNode.h"

#include "rendering/scene/Scene.h"
#include <imgui.h>

RenderPipelineNode::ExecuteCallback RTReflectionsNode::construct(Scene& scene, Registry& reg)
{
    Texture& reflectionsImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("Reflections", reflectionsImage);

    BindingSet& rtMeshDataBindingSet = *reg.getBindingSet("SceneRTMeshDataSet");
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStage::RTRayGen | ShaderStage::RTClosestHit, &sceneTLAS },
                                                         { 1, ShaderStage::RTRayGen, &reflectionsImage, ShaderBindingType::StorageImage },
                                                         { 2, ShaderStage::RTRayGen, reg.getTexture("SceneMaterial"), ShaderBindingType::TextureSampler },
                                                         { 3, ShaderStage::RTRayGen, reg.getTexture("SceneNormalVelocity"), ShaderBindingType::TextureSampler },
                                                         { 4, ShaderStage::RTRayGen, reg.getTexture("SceneDepth"), ShaderBindingType::TextureSampler },
                                                         { 5, ShaderStage::RTRayGen | ShaderStage::RTClosestHit, reg.getBuffer("SceneCameraData") },
                                                         { 6, ShaderStage::RTRayGen, reg.getTexture("SceneEnvironmentMap"), ShaderBindingType::TextureSampler } });

    ShaderFile raygen { "rt-reflections/raygen.rgen" };
    ShaderFile defaultMissShader { "rt-reflections/miss.rmiss" };
    ShaderFile shadowMissShader { "rt-reflections/shadow.rmiss" };
    HitGroup mainHitGroup { ShaderFile("rt-reflections/default.rchit"), ShaderFile("rt-reflections/masked.rahit") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { defaultMissShader, shadowMissShader } };

    StateBindings stateDataBindings;
    stateDataBindings.at(0, frameBindingSet);
    stateDataBindings.at(1, rtMeshDataBindingSet);
    stateDataBindings.at(2, materialBindingSet);
    stateDataBindings.at(3, lightBindingSet);

    constexpr uint32_t maxRecursionDepth = 2; // raygen -> closest hit -> shadow ray
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        cmdList.clearTexture(reflectionsImage, ClearColor::black());
        cmdList.setRayTracingState(rtState);

        static float injectedAmbient = 500.0f;
        ImGui::SliderFloat("Injected ambient", &injectedAmbient, 0.0f, 1'000.0f);
        cmdList.setNamedUniform("ambientAmount", injectedAmbient * scene.lightPreExposureValue());

        static float mirrorRoughnessThreshold = 0.2f;
        static float fullyDiffuseRoughnessThreshold = 0.96f;
        ImGui::SliderFloat("Perfect mirror threshold", &mirrorRoughnessThreshold, 0.0f, fullyDiffuseRoughnessThreshold - 0.01f);
        ImGui::SliderFloat("Fully diffuse threshold", &fullyDiffuseRoughnessThreshold, mirrorRoughnessThreshold + 0.01f, 1.0f);
        cmdList.setNamedUniform("mirrorRoughnessThreshold", mirrorRoughnessThreshold);
        cmdList.setNamedUniform("fullyDiffuseRoughnessThreshold", fullyDiffuseRoughnessThreshold);

        cmdList.setNamedUniform("environmentMultiplier", scene.exposedEnvironmentMultiplier());

        cmdList.traceRays(appState.windowExtent());
    };
}
