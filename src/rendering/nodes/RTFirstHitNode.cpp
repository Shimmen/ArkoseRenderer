#include "RTFirstHitNode.h"

#include "RTAccelerationStructures.h"
#include <half.hpp>
#include <imgui.h>

// Shader headers
#include "RTData.h"

RTFirstHitNode::RTFirstHitNode(Scene& scene)
    : RenderGraphNode(RTFirstHitNode::name())
    , m_scene(scene)
{
}

std::string RTFirstHitNode::name()
{
    return "rt-firsthit";
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

RenderGraphNode::ExecuteCallback RTFirstHitNode::constructFrame(Registry& reg) const
{
    Texture& storageImage = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("image", storageImage);

    BindingSet& environmentBindingSet = reg.createBindingSet({ { 0, ShaderStageRTMiss, reg.getTexture("scene", "environmentMap").value(), ShaderBindingType::TextureSampler } });
    BindingSet& materialBindingSet = reg.createBindingSet({ { 0, ShaderStageRTClosestHit, m_scene.globalMaterialDataBuffer() },
                                                            { 1, ShaderStageRTClosestHit, m_scene.globalMaterialTextureArray(), SCENE_MAX_TEXTURES } });

    TopLevelAS& sceneTLAS = m_scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, &sceneTLAS },
                                                         { 1, ShaderStageRTRayGen, reg.getBuffer("scene", "camera") },
                                                         { 2, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage } });

    ShaderFile raygen = ShaderFile("rt-firsthit/raygen.rgen");
    HitGroup mainHitGroup { ShaderFile("rt-firsthit/closestHit.rchit") };
    ShaderFile missShader { ShaderFile("rt-firsthit/miss.rmiss") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { missShader } };

    uint32_t maxRecursionDepth = 1;
    RayTracingState& rtState = reg.createRayTracingState(sbt, { &frameBindingSet, m_objectDataBindingSet, &materialBindingSet, &environmentBindingSet }, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.setRayTracingState(rtState);

        cmdList.bindSet(frameBindingSet, 0);
        cmdList.bindSet(*m_objectDataBindingSet, 1);
        cmdList.bindSet(materialBindingSet, 2);
        cmdList.bindSet(environmentBindingSet, 3);

        cmdList.traceRays(appState.windowExtent());
    };
}
