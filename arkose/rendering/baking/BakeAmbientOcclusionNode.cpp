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
    ARKOSE_ASSERT(outputTexture.format() == Texture::Format::R8); // TODO!

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
                                                       .format = Texture::Format::RGBA8,
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
    
    // ..

    //

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        if (StaticMesh const* staticMesh = scene.staticMeshForInstance(m_instanceToBake)) {

            StaticMeshLOD const& lod0 = staticMesh->lodAtIndex(m_meshLodIdxToBake);
            StaticMeshSegment const& meshSegment = lod0.meshSegments[m_meshSegmentIdxToBake];

            if (meshSegment.blendMode == BlendMode::Translucent) {
                ARKOSE_LOG(Error, "BakeAmbientOcclusionNode: mesh at LOD{} segment {} is translucent, so can't bake ambient occlusion", m_meshLodIdxToBake, m_meshSegmentIdxToBake);
                return;
            }

            DrawCallDescription drawCall = meshSegment.vertexAllocation.asDrawCallDescription();

            // Bake the parameterization down so we can refer back to the triangles given a pixel
            cmdList.beginRendering(bakeParamsRenderState, ClearValue::blackAtMaxDepth(), true);
            cmdList.bindVertexBuffer(scene.vertexManager().nonPositionVertexBuffer(), bakeParamsVertexLayout.packedVertexSize(), 0);
            cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());
            cmdList.issueDrawCall(drawCall);
            cmdList.endRendering();

            // For each pixel, ray trace to calculate the ambient occlusion (on the output texture)
            // cmdList.setRayTracingState(..);
            // cmdList.traceRays(bakeExtent);

        } else {
            ARKOSE_LOG(Error, "BakeAmbientOcclusionNode: the supplied mesh instance is not in the current scene!");
        }
    };
}
