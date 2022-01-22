#include "RTFirstHitNode.h"

#include <half.hpp>
#include <imgui.h>

// Shader headers
#include "RTData.h"

RenderPipelineNode::ExecuteCallback RTFirstHitNode::construct(Scene& scene, Registry& reg)
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
    reg.publish("RTFirstHit", storageImage);

    BindingSet& environmentBindingSet = reg.createBindingSet({ { 0, ShaderStageRTMiss, reg.getTexture("SceneEnvironmentMap"), ShaderBindingType::TextureSampler } });
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& frameBindingSet = reg.createBindingSet({ { 0, ShaderStageRTRayGen, &sceneTLAS },
                                                         { 1, ShaderStageRTRayGen, reg.getBuffer("SceneCameraData") },
                                                         { 2, ShaderStageRTRayGen, &storageImage, ShaderBindingType::StorageImage } });

    ShaderFile raygen = ShaderFile("rt-firsthit/raygen.rgen");
    HitGroup mainHitGroup { ShaderFile("rt-firsthit/closestHit.rchit") };
    ShaderFile missShader { ShaderFile("rt-firsthit/miss.rmiss") };
    ShaderBindingTable sbt { raygen, { mainHitGroup }, { missShader } };

    StateBindings stateBindings;
    stateBindings.at(0, frameBindingSet);
    stateBindings.at(1, objectDataBindingSet);
    stateBindings.at(2, materialBindingSet);
    stateBindings.at(3, environmentBindingSet);

    uint32_t maxRecursionDepth = 1;
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateBindings, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        cmdList.setRayTracingState(rtState);

        cmdList.bindSet(frameBindingSet, 0);
        cmdList.bindSet(objectDataBindingSet, 1);
        cmdList.bindSet(materialBindingSet, 2);
        cmdList.bindSet(environmentBindingSet, 3);

        cmdList.traceRays(appState.windowExtent());
    };
}
