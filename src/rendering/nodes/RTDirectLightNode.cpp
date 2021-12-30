#include "RTDirectLightNode.h"

#include "RTAccelerationStructures.h"
#include <imgui.h>

// Shader headers
#include "RTData.h"

RTDirectLightNode::RTDirectLightNode(Scene& scene)
    : RenderGraphNode(RTDirectLightNode::name())
    , m_scene(scene)
{
}

std::string RTDirectLightNode::name()
{
    return "rt-direct-light";
}

void RTDirectLightNode::constructNode(Registry& nodeReg)
{
    const VertexLayout vertexLayout = { VertexComponent::Normal3F,
                                        VertexComponent::TexCoord2F };

    std::vector<RTTriangleMesh> rtMeshes {};
    m_scene.forEachMesh([&](size_t meshIdx, Mesh& mesh) {
        const DrawCallDescription& drawCallDesc = mesh.drawCallDescription(vertexLayout, m_scene);
        rtMeshes.push_back({ .firstVertex = drawCallDesc.vertexOffset,
                             .firstIndex = (int32_t)drawCallDesc.firstIndex,
                             .materialIndex = mesh.materialIndex().value_or(0) });
    });

    Buffer& indexBuffer = m_scene.globalIndexBuffer();
    Buffer& vertexBuffer = m_scene.globalVertexBufferForLayout(vertexLayout);

    Buffer& meshBuffer = nodeReg.createBuffer(rtMeshes, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
    m_objectDataBindingSet = &nodeReg.createBindingSet({ { 0, ShaderStage(ShaderStageRTClosestHit | ShaderStageRTAnyHit), &meshBuffer },
                                                         { 1, ShaderStage(ShaderStageRTClosestHit | ShaderStageRTAnyHit), &indexBuffer },
                                                         { 2, ShaderStage(ShaderStageRTClosestHit | ShaderStageRTAnyHit), &vertexBuffer } });
}

RenderGraphNode::ExecuteCallback RTDirectLightNode::constructFrame(Registry& reg) const
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("target", storageImage);

    BindingSet& materialBindingSet = m_scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("lightSet");

    TopLevelAS& sceneTLAS = m_scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), &sceneTLAS },
                                                         { 1, ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), reg.getBuffer("camera") },
                                                         { 2, ShaderStageRTRayGen, reg.getTexture("SceneEnvironmentMap"), ShaderBindingType::TextureSampler },
                                                         { 3, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage } });

    ShaderFile raygen { "rt-direct-light/raygen.rgen" };
    ShaderFile defaultMissShader { "rt-direct-light/miss.rmiss" };
    ShaderFile shadowMissShader { "rt-direct-light/shadow.rmiss" };
    HitGroup mainHitGroup { ShaderFile("rt-direct-light/default.rchit"), ShaderFile("rt-direct-light/masked.rahit") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { defaultMissShader, shadowMissShader } };

    StateBindings stateDataBindings;
    stateDataBindings.at(0, frameBindingSet);
    stateDataBindings.at(1, *m_objectDataBindingSet);
    stateDataBindings.at(2, materialBindingSet);
    stateDataBindings.at(3, lightBindingSet);

    constexpr uint32_t maxRecursionDepth = 2; // raygen -> closest hit -> shadow ray
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.setRayTracingState(rtState);

        float ambientLx = m_scene.ambient();
        static bool useSceneAmbient = false;
        ImGui::Checkbox("Use scene ambient light", &useSceneAmbient);
        if (!useSceneAmbient) {
            static float injectedAmbientLx = 200.0f;
            ImGui::SliderFloat("Injected ambient (lx)", &injectedAmbientLx, 0.0f, 10'000.0f, "%.0f");
            ambientLx = injectedAmbientLx;
        }

        // TODO: Do we still want the exposed variants when we use this for indirect light stuff?
        // TODO: It would be nice to actually support the names uniforms API for ray tracing..
        cmdList.pushConstant(ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), ambientLx * m_scene.lightPreExposureValue(), 0);
        cmdList.pushConstant(ShaderStage(ShaderStageRTRayGen | ShaderStageRTClosestHit), m_scene.exposedEnvironmentMultiplier(), sizeof(float));

        cmdList.traceRays(appState.windowExtent());
    };
}
