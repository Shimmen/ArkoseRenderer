#include "PickingNode.h"

#include "system/Input.h"
#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include "rendering/StaticMesh.h"
#include "scene/camera/CameraController.h"
#include "utility/Profiling.h"
#include <imgui.h>
#include <ark/vector.h>

// Shader headers
#include "shaders/shared/PickingData.h"

RenderPipelineNode::ExecuteCallback PickingNode::construct(GpuScene& scene, Registry& reg)
{
    Buffer& resultBuffer = reg.createBuffer(sizeof(PickingData), Buffer::Usage::Readback);
    resultBuffer.setStride(sizeof(PickingData));

    Texture& indexTexture = reg.createTexture2D(pipeline().outputResolution(), Texture::Format::R32Uint);
    Texture& depthTexture = reg.createTexture2D(pipeline().outputResolution(), Texture::Format::Depth32F);
    RenderTarget& indexMapRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &indexTexture },
                                                                  { RenderTarget::AttachmentType::Depth, &depthTexture } });

    Shader drawIndexShader = Shader::createBasicRasterize("picking/drawIndices.vert", "picking/drawIndices.frag");
    RenderStateBuilder renderStateBuilder(indexMapRenderTarget, drawIndexShader, VertexLayout { VertexComponent::Position3F });
    renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneObjectSet"));
    RenderState& drawIndicesState = reg.createRenderState(renderStateBuilder);

    
    Shader collectorShader = Shader::createCompute("picking/collectData.comp");
    BindingSet& collectIndexBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(resultBuffer, ShaderStage::Compute),
                                                                ShaderBinding::storageTexture(indexTexture, ShaderStage::Compute),
                                                                ShaderBinding::sampledTexture(depthTexture, ShaderStage::Compute),
                                                                ShaderBinding::constantBuffer(*reg.getBuffer("SceneCameraData"), ShaderStage::Compute) });
    StateBindings collectIndexStateBindings;
    collectIndexStateBindings.at(0, collectIndexBindingSet);

    ComputeState& collectState = reg.createComputeState(collectorShader, collectIndexStateBindings);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        // TODO: Implement some proper CPU readback context so we know for sure that the previous result
        // is ready at this point. Just because it's from the previous frame doesn't mean it must be done.
        // What if we submit the queue and immediately start work on the next frame before the first is
        // even started? And many more similar scenarios.
        if (m_pendingDeferredResult.has_value()) {
            processDeferredResult(cmdList, scene, m_pendingDeferredResult.value());
            m_pendingDeferredResult.reset();
        }

        auto& input = Input::instance();
        vec2 pickLocation = input.mousePosition();
        bool meshSelectPick = not input.isGuiUsingMouse() && input.didClickButton(Button::Left);
        bool focusDepthPick = not input.isGuiUsingMouse() && input.didClickButton(Button::Middle);

        if (meshSelectPick || focusDepthPick) {

            if (EditorGizmo* gizmo = scene.scene().raycastScreenPointAgainstEditorGizmos(pickLocation)) {
                if (meshSelectPick) {
                    scene.scene().setSelectedObject(gizmo->editorObject());
                } else if (focusDepthPick) {
                    setFocusDepth(scene, gizmo->distanceFromCamera());
                }
                return;
            }

            ClearValue clearValue { .color = ClearColor::srgbColor(1, 0, 1),
                                    .depth = 1.0f };

            cmdList.beginRendering(drawIndicesState, clearValue);
            cmdList.setNamedUniform("projectionFromWorld", scene.camera().viewProjectionMatrix());

            cmdList.bindVertexBuffer(scene.vertexManager().positionVertexBuffer(), drawIndicesState.vertexLayout().packedVertexSize(), 0);
            cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

            u32 drawIdx = 0;

            for (auto& instance : scene.staticMeshInstances()) {
                if (StaticMesh const* staticMesh = scene.staticMeshForHandle(instance->mesh())) {

                    // TODO: Pick LOD properly (i.e. the same as drawn in the main passes)
                    StaticMeshLOD const& lod = staticMesh->lodAtIndex(0);

                    for (StaticMeshSegment const& meshSegment : lod.meshSegments) {
                        DrawCallDescription drawCall = meshSegment.vertexAllocation.asDrawCallDescription();
                        drawCall.firstInstance = drawIdx++;
                        cmdList.issueDrawCall(drawCall);
                    }
                }
            }

            for (auto& instance : scene.skeletalMeshInstances()) {
                for (SkinningVertexMapping const& skinningVertexMapping : instance->skinningVertexMappings()) {
                    DrawCallDescription drawCall = skinningVertexMapping.skinnedTarget.asDrawCallDescription();
                    drawCall.firstInstance = drawIdx++;
                    cmdList.issueDrawCall(drawCall);
                }
            }

            cmdList.endRendering();
            
            cmdList.textureWriteBarrier(indexTexture);
            cmdList.textureWriteBarrier(depthTexture);

            cmdList.setComputeState(collectState);
            cmdList.setNamedUniform("mousePosition", pickLocation);
            cmdList.dispatch(indexTexture.extent(), { 16, 16, 1 });

            m_pendingDeferredResult = DeferredResult { .resultBuffer = &resultBuffer,
                                                       .selectMesh = meshSelectPick,
                                                       .specifyFocusDepth = focusDepthPick };
        }
    };
}

void PickingNode::processDeferredResult(CommandList& cmdList, GpuScene& scene, const DeferredResult& deferredResult)
{
    // At least one must be specified
    ARKOSE_ASSERT(deferredResult.selectMesh || deferredResult.specifyFocusDepth);
    
    PickingData pickingData;
    cmdList.slowBlockingReadFromBuffer(*deferredResult.resultBuffer, 0, sizeof(pickingData), &pickingData);

    if (deferredResult.selectMesh) {
        int selectedIdx = pickingData.meshIdx;

        i32 drawIdx = 0;
        
        for (auto& instance : scene.staticMeshInstances()) {
            if (const StaticMesh* staticMesh = scene.staticMeshForHandle(instance->mesh())) {

                // TODO: Pick LOD properly (i.e. the same as drawn in the main passes)
                const StaticMeshLOD& lod = staticMesh->lodAtIndex(0);

                for (StaticMeshSegment const& meshSegment : lod.meshSegments) {
                    (void)meshSegment;

                    if (drawIdx == selectedIdx) {
                        // TODO: This will break if/when we resize the instance vector
                        scene.scene().setSelectedObject(*instance);
                        return;
                    }

                    drawIdx += 1;
                }
            }
        }

         for (auto& instance : scene.skeletalMeshInstances()) {
            for (SkinningVertexMapping const& skinningVertexMapping : instance->skinningVertexMappings()) {
                 (void)skinningVertexMapping;

                 if (drawIdx == selectedIdx) {
                     // TODO: This will break if/when we resize the instance vector
                     scene.scene().setSelectedObject(*instance);
                     return;
                 }

                 drawIdx += 1;
            }
        }

        // If no mesh was found, we must have clicked on the background so deselect current
        scene.scene().clearSelectedObject();
    }

    if (deferredResult.specifyFocusDepth) {
        setFocusDepth(scene, pickingData.depth);
    }
}

void PickingNode::setFocusDepth(GpuScene& scene, float focusDepth)
{
    Camera& camera = scene.camera();

    if (CameraController* cameraController = camera.controller()) {
        cameraController->setTargetFocusDepth(focusDepth);
    } else {
        camera.setFocusDepth(focusDepth);
    }
}
