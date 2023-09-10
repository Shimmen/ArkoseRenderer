#include "PrepassNode.h"

#include "rendering/util/ScopedDebugZone.h"
#include <imgui.h>

PrepassNode::PrepassNode(PrepassMode mode)
    : m_mode(mode)
{
}

RenderPipelineNode::ExecuteCallback PrepassNode::construct(GpuScene& scene, Registry& reg)
{
    RenderState& prepassOpaqueRenderState = makeRenderState(reg, scene, PassType::Opaque);
    //Buffer& opaqueIndirectDrawCmdsBuffer = *reg.getBuffer("MainViewOpaqueDrawCmds");
    //Buffer& opaqueIndirectDrawCountBuffer = *reg.getBuffer("MainViewOpaqueDrawCount");

    RenderState& prepassMaskedRenderState = makeRenderState(reg, scene, PassType::Masked);
    //Buffer& maskedIndirectDrawCmdsBuffer = *reg.getBuffer("MainViewMaskedDrawCmds");
    //Buffer& maskedIndirectDrawCountBuffer = *reg.getBuffer("MainViewMaskedDrawCount");

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        auto setCommonConstants = [&]() {
            cmdList.setNamedUniform("depthOffset", 0.00005f);
            cmdList.setNamedUniform("projectionFromWorld", scene.camera().viewProjectionMatrix());
        };

        if (m_mode == PrepassMode::OpaqueObjectsOnly || m_mode == PrepassMode::AllOpaquePixels) {
            ScopedDebugZone zone { cmdList, "Opaque" };

            cmdList.beginRendering(prepassOpaqueRenderState, ClearValue::blackAtMaxDepth());
            setCommonConstants();

            cmdList.bindVertexBuffer(scene.vertexManager().positionVertexBuffer());
            cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

            //cmdList.drawIndirect(opaqueIndirectDrawCmdsBuffer, opaqueIndirectDrawCountBuffer);
            for (auto const& instance : scene.staticMeshInstances()) {
                if (StaticMesh const* staticMesh = scene.staticMeshForInstance(*instance)) {

                    constexpr u32 lodIdx = 0;
                    StaticMeshLOD const& lod = staticMesh->lodAtIndex(lodIdx);

                    if (!staticMesh->hasNonTranslucentSegments()) {
                        continue;
                    }

                    //if (not cameraFrustum.includesSphere(staticMesh->boundingSphere())) {
                    //    continue;
                    //}

                    for (u32 segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
                        StaticMeshSegment const& meshSegment = lod.meshSegments[segmentIdx];
                        if (meshSegment.blendMode == BlendMode::Opaque) {
                            DrawCallDescription drawCall = meshSegment.vertexAllocation.asDrawCallDescription();
                            drawCall.firstInstance = instance->drawableHandleForSegmentIndex(segmentIdx).indexOfType<u32>();
                            cmdList.issueDrawCall(drawCall);
                        }
                    }
                }
            }

            cmdList.endRendering();
        }

        if (m_mode == PrepassMode::AllOpaquePixels) {
            ScopedDebugZone zone { cmdList, "Masked" };

            cmdList.beginRendering(prepassMaskedRenderState);
            setCommonConstants();

            cmdList.bindVertexBuffer(scene.vertexManager().positionVertexBuffer(), 0);
            cmdList.bindVertexBuffer(scene.vertexManager().nonPositionVertexBuffer(), 1);
            cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

            //cmdList.drawIndirect(maskedIndirectDrawCmdsBuffer, maskedIndirectDrawCountBuffer);
            for (auto const& instance : scene.staticMeshInstances()) {
                if (StaticMesh const* staticMesh = scene.staticMeshForInstance(*instance)) {

                    constexpr u32 lodIdx = 0;
                    StaticMeshLOD const& lod = staticMesh->lodAtIndex(lodIdx);

                    if (!staticMesh->hasNonTranslucentSegments()) {
                        continue;
                    }

                    //if (not cameraFrustum.includesSphere(staticMesh->boundingSphere())) {
                    //    continue;
                    //}

                    for (u32 segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
                        StaticMeshSegment const& meshSegment = lod.meshSegments[segmentIdx];
                        if (meshSegment.blendMode == BlendMode::Masked) {
                            DrawCallDescription drawCall = meshSegment.vertexAllocation.asDrawCallDescription();
                            drawCall.firstInstance = instance->drawableHandleForSegmentIndex(segmentIdx).indexOfType<u32>();
                            cmdList.issueDrawCall(drawCall);
                        }
                    }
                }
            }

            cmdList.endRendering();
        }
    };
}

RenderState& PrepassNode::makeRenderState(Registry& reg, GpuScene const& scene, PassType type) const
{
    Shader shader {};
    std::vector<VertexLayout> vertexLayout {};
    const char* stateName = "";
    LoadOp loadOp = LoadOp::Clear;

    switch (type) {
    case PassType::Opaque:
        shader = Shader::createVertexOnly("forward/prepass.vert");
        vertexLayout = { scene.vertexManager().positionVertexLayout() };
        stateName = "PrepassOpaque";
        loadOp = LoadOp::Clear;
        break;
    case PassType::Masked:
        shader = Shader::createBasicRasterize("forward/prepassMasked.vert", "forward/prepassMasked.frag");
        vertexLayout = { scene.vertexManager().positionVertexLayout(), scene.vertexManager().nonPositionVertexLayout() };
        stateName = "PrepassMasked";
        loadOp = LoadOp::Load;
        break;
    }

    Texture* sceneDepth = reg.getTexture("SceneDepth");
    RenderTarget& prepassRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, sceneDepth, loadOp, StoreOp::Store } });
    
    RenderStateBuilder renderStateBuilder { prepassRenderTarget, shader, std::move(vertexLayout) };

    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.stencilMode = StencilMode::AlwaysWrite;

    switch (type) {
    case PassType::Opaque:
        renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneObjectSet"));//reg.getBindingSet("MainViewCulledDrawablesOpaqueSet"));
        break;
    case PassType::Masked:
        renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneObjectSet"));//reg.getBindingSet("MainViewCulledDrawablesMaskedSet"));
        renderStateBuilder.stateBindings().at(1, scene.globalMaterialBindingSet());
        break;
    }

    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName(stateName);

    return renderState;
}
