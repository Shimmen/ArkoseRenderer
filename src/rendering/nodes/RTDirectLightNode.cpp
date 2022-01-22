#include "RTDirectLightNode.h"

#include <imgui.h>

// Shader headers
#include "RTData.h"

RenderPipelineNode::ExecuteCallback RTDirectLightNode::construct(Scene& scene, Registry& reg)
{
    ///////////////////////
    // constructNode

    // TODO: All of this is reusable for all rt stuff, should go in Scene!

    const VertexLayout vertexLayout = { VertexComponent::Normal3F,
                                        VertexComponent::TexCoord2F };

    std::vector<RTTriangleMesh> rtMeshes {};
    scene.forEachMesh([&](size_t meshIdx, Mesh& mesh) {
        const DrawCallDescription& drawCallDesc = mesh.drawCallDescription(vertexLayout, scene);
        rtMeshes.push_back({ .firstVertex = drawCallDesc.vertexOffset,
                             .firstIndex = (int32_t)drawCallDesc.firstIndex,
                             .materialIndex = mesh.materialIndex().value_or(0) });
    });

    Buffer& indexBuffer = scene.globalIndexBuffer();
    Buffer& vertexBuffer = scene.globalVertexBufferForLayout(vertexLayout);

    Buffer& meshBuffer = reg.createBuffer(rtMeshes, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    BindingSet& objectDataBindingSet = reg.createBindingSet({ { 0, ShaderStageRTClosestHit, &meshBuffer },
                                                              { 1, ShaderStageRTClosestHit, &indexBuffer },
                                                              { 2, ShaderStageRTClosestHit, &vertexBuffer } });
    ///////////////////////

    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("RTDirectLight", storageImage);

    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), &sceneTLAS },
                                                         { 1, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), reg.getBuffer("SceneCameraData") },
                                                         { 2, ShaderStageRTRayGen, reg.getTexture("SceneEnvironmentMap"), ShaderBindingType::TextureSampler },
                                                         { 3, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage } });

    ShaderFile raygen { "rt-direct-light/raygen.rgen" };
    ShaderFile defaultMissShader { "rt-direct-light/miss.rmiss" };
    ShaderFile shadowMissShader { "rt-direct-light/shadow.rmiss" };
    HitGroup mainHitGroup { ShaderFile("rt-direct-light/default.rchit"), ShaderFile("rt-direct-light/masked.rahit") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { defaultMissShader, shadowMissShader } };

    StateBindings stateDataBindings;
    stateDataBindings.at(0, frameBindingSet);
    stateDataBindings.at(1, objectDataBindingSet);
    stateDataBindings.at(2, materialBindingSet);
    stateDataBindings.at(3, lightBindingSet);

    constexpr uint32_t maxRecursionDepth = 2; // raygen -> closest hit -> shadow ray
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        cmdList.setRayTracingState(rtState);

        float ambientLx = scene.ambient();
        static bool useSceneAmbient = false;
        ImGui::Checkbox("Use scene ambient light", &useSceneAmbient);
        if (!useSceneAmbient) {
            static float injectedAmbientLx = 200.0f;
            ImGui::SliderFloat("Injected ambient (lx)", &injectedAmbientLx, 0.0f, 10'000.0f, "%.0f");
            ambientLx = injectedAmbientLx;
        }

        // TODO: Do we still want the exposed variants when we use this for indirect light stuff?
        // TODO: It would be nice to actually support the names uniforms API for ray tracing..
        cmdList.pushConstant(ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), ambientLx * scene.lightPreExposureValue(), 0);
        cmdList.pushConstant(ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), scene.exposedEnvironmentMultiplier(), sizeof(float));

        cmdList.traceRays(appState.windowExtent());
    };
}
