#include "RTFirstHitNode.h"

#include <half.hpp>
#include <imgui.h>

// Shader headers
#include "RTData.h"

RTFirstHitNode::RTFirstHitNode(Scene& scene)
    : m_scene(scene)
{
}

void RTFirstHitNode::constructNode(Registry& nodeReg)
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
    m_objectDataBindingSet = &nodeReg.createBindingSet({ { 0, ShaderStageRTClosestHit, &meshBuffer },
                                                         { 1, ShaderStageRTClosestHit, &indexBuffer },
                                                         { 2, ShaderStageRTClosestHit, &vertexBuffer } });
}

RenderPipelineNode::ExecuteCallback RTFirstHitNode::constructFrame(Registry& reg) const
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("RTFirstHit", storageImage);

    BindingSet& environmentBindingSet = reg.createBindingSet({ { 0, ShaderStageRTMiss, reg.getTexture("SceneEnvironmentMap"), ShaderBindingType::TextureSampler } });
    BindingSet& materialBindingSet = m_scene.globalMaterialBindingSet();

    TopLevelAS& sceneTLAS = m_scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, &sceneTLAS },
                                                         { 1, ShaderStageRTRayGen, reg.getBuffer("SceneCameraData") },
                                                         { 2, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage } });

    ShaderFile raygen = ShaderFile("rt-firsthit/raygen.rgen");
    HitGroup mainHitGroup { ShaderFile("rt-firsthit/closestHit.rchit") };
    ShaderFile missShader { ShaderFile("rt-firsthit/miss.rmiss") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { missShader } };

    StateBindings stateBindings;
    stateBindings.at(0, frameBindingSet);
    stateBindings.at(1, *m_objectDataBindingSet);
    stateBindings.at(2, materialBindingSet);
    stateBindings.at(3, environmentBindingSet);

    uint32_t maxRecursionDepth = 1;
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateBindings, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        cmdList.setRayTracingState(rtState);

        cmdList.bindSet(frameBindingSet, 0);
        cmdList.bindSet(*m_objectDataBindingSet, 1);
        cmdList.bindSet(materialBindingSet, 2);
        cmdList.bindSet(environmentBindingSet, 3);

        cmdList.traceRays(appState.windowExtent());
    };
}
