#include "BakeAmbientOcclusionNode.h"

#include "core/Logging.h"
#include "rendering/GpuScene.h"

BakeAmbientOcclusionNode::BakeAmbientOcclusionNode(StaticMeshInstance& instanceToBake, u32 meshLodIdxToBake, u32 meshSegmentIdxToBake)
    : m_instanceToBake(instanceToBake)
    , m_meshLodIdxToBake(meshLodIdxToBake)
    , m_meshSegmentIdxToBake(meshSegmentIdxToBake)
{
}

RenderPipelineNode::ExecuteCallback BakeAmbientOcclusionNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& outputTexture = *reg.windowRenderTarget().colorAttachments()[0].texture;
    ARKOSE_ASSERT(outputTexture.format() == Texture::Format::R8Uint); // TODO!

    Extent2D const& bakeExtent = reg.windowRenderTarget().extent();

    //
    // Construct for bake to parameterization map
    //

    Texture& triangleIdxTexture = reg.createTexture({ .extent = bakeExtent,
                                                      .format = Texture::Format::R32Uint,
                                                      .filter = Texture::Filters::nearest(),
                                                      .wrapMode = ImageWrapModes::clampAllToEdge(),
                                                      .mipmap = Texture::Mipmap::None });
    Texture& barycentricsTexture = reg.createTexture({ .extent = bakeExtent,
                                                       .format = Texture::Format::RGBA16F,
                                                       .filter = Texture::Filters::nearest(),
                                                       .wrapMode = ImageWrapModes::clampAllToEdge(),
                                                       .mipmap = Texture::Mipmap::None });

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &triangleIdxTexture },
                                                          { RenderTarget::AttachmentType::Color1, &barycentricsTexture } });

    Shader bakeParamsParamsShader = Shader::createBasicRasterize("baking/bakeParameterization.vert", "baking/bakeParameterization.frag");

    // Ensure we don't try to load the unused components from the vertex buffer
    VertexLayout& bakeParamsVertexLayout = reg.allocate<VertexLayout>();
    bakeParamsVertexLayout = scene.vertexManager().nonPositionVertexLayout().replaceAllWithPaddingBut(VertexComponent::TexCoord2F);

    RenderStateBuilder bakeParamsStateBuilder { renderTarget, bakeParamsParamsShader, bakeParamsVertexLayout };
    bakeParamsStateBuilder.primitiveType = PrimitiveType::Triangles;
    bakeParamsStateBuilder.cullBackfaces = false;
    bakeParamsStateBuilder.writeDepth = false;
    bakeParamsStateBuilder.testDepth = false;

    RenderState& bakeParamsRenderState = reg.createRenderState(bakeParamsStateBuilder);

    //
    // Construct for ray tracing step
    //

    ShaderFile raygen { "baking/ao/bakeAmbientOcclusion.rgen" };
    ShaderFile missShader { "baking/ao/bakeAmbientOcclusion.rmiss" };
    HitGroup opaqueHitGroup { ShaderFile("baking/ao/bakeAmbientOcclusion.rchit") };
    HitGroup maskedHitGroup { ShaderFile("baking/ao/bakeAmbientOcclusion.rchit"),
                              ShaderFile("baking/ao/bakeAmbientOcclusion.rahit") };

    ShaderBindingTable sbt;
    sbt.setRayGenerationShader(raygen);
    sbt.setMissShader(0, missShader);
    sbt.setHitGroup(0, opaqueHitGroup);
    sbt.setHitGroup(1, maskedHitGroup);

    BindingSet& bakeBindingSet = reg.createBindingSet({ ShaderBinding::topLevelAccelerationStructure(scene.globalTopLevelAccelerationStructure(), ShaderStage::RTRayGen),
                                                        ShaderBinding::sampledTexture(triangleIdxTexture, ShaderStage::RTRayGen),
                                                        ShaderBinding::sampledTexture(barycentricsTexture, ShaderStage::RTRayGen),
                                                        ShaderBinding::storageTexture(outputTexture, ShaderStage::RTRayGen) });

    StateBindings stateDataBindings;
    stateDataBindings.at(0, bakeBindingSet);
    stateDataBindings.at(1, *reg.getBindingSet("SceneRTMeshDataSet"));
    stateDataBindings.at(2, scene.globalMaterialBindingSet());

    constexpr uint32_t maxRecursionDepth = 1; // raygen -> closest/any hit
    RayTracingState& aoRayTracingState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    //

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        if (StaticMesh const* staticMesh = scene.staticMeshForInstance(m_instanceToBake)) {

            StaticMeshLOD const& lod0 = staticMesh->lodAtIndex(m_meshLodIdxToBake);
            StaticMeshSegment const& meshSegment = lod0.meshSegments[m_meshSegmentIdxToBake];

            if (meshSegment.blendMode == BlendMode::Translucent) {
                ARKOSE_LOG(Error, "BakeAmbientOcclusionNode: mesh at LOD{} segment {} is translucent, so can't bake ambient occlusion", m_meshLodIdxToBake, m_meshSegmentIdxToBake);
                return;
            }

            // NOTE: This is hard-coded to 0 in the baking shader atm, ensure we match these two up! Make it a uniform value.
            ARKOSE_ASSERT(meshSegment.staticMeshHandle.index() == 0);

            DrawCallDescription drawCall = meshSegment.vertexAllocation.asDrawCallDescription();

            // Bake the parameterization down so we can refer back to the triangles given a pixel
            cmdList.beginRendering(bakeParamsRenderState, ClearValue::blackAtMaxDepth(), true);
            cmdList.bindVertexBuffer(scene.vertexManager().nonPositionVertexBuffer(), bakeParamsVertexLayout.packedVertexSize(), 0);
            cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());
            cmdList.issueDrawCall(drawCall);
            cmdList.endRendering();

            // For each pixel, ray trace to calculate the ambient occlusion (on the output texture)
            cmdList.setRayTracingState(aoRayTracingState);
            cmdList.traceRays(bakeExtent);

        } else {
            ARKOSE_LOG(Error, "BakeAmbientOcclusionNode: the supplied mesh instance is not in the current scene!");
        }
    };
}
