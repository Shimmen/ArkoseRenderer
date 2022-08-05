#include "PickingNode.h"

#include "rendering/GpuScene.h"
#include "rendering/StaticMesh.h"
#include "scene/camera/CameraController.h"
#include "utility/Input.h"
#include "utility/Profiling.h"
#include <imgui.h>
#include <ark/vector.h>

// Shader headers
#include "PickingData.h"

RenderPipelineNode::ExecuteCallback PickingNode::construct(GpuScene& scene, Registry& reg)
{
    Buffer& resultBuffer = reg.createBuffer(sizeof(PickingData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::Readback);

    Texture& indexTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R32Uint);
    Texture& depthTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::Depth32F);
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
    ComputeState& collectState = reg.createComputeState(collectorShader, { &collectIndexBindingSet });

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
        bool meshSelectPick = input.didClickButton(Button::Left);
        bool focusDepthPick = input.didClickButton(Button::Middle);

        if (meshSelectPick || focusDepthPick) {

            scene.ensureDrawCallIsAvailableForAll({ VertexComponent::Position3F });

            ClearValue clearValue { .color = ClearColor::srgbColor(1, 0, 1),
                                    .depth = 1.0f };

            cmdList.beginRendering(drawIndicesState, clearValue);
            cmdList.setNamedUniform("projectionFromWorld", scene.camera().viewProjectionMatrix());

            cmdList.bindVertexBuffer(scene.globalVertexBufferForLayout({ VertexComponent::Position3F }));
            cmdList.bindIndexBuffer(scene.globalIndexBuffer(), scene.globalIndexBufferType());

            uint32_t drawIdx = 0;
            for (StaticMeshInstance& instance : scene.scene().staticMeshInstances()) {
                if (const StaticMesh* staticMesh = scene.staticMeshForHandle(instance.mesh)) {

                    // TODO: Pick LOD properly (i.e. the same as drawn in the main passes)
                    const StaticMeshLOD& lod = staticMesh->lodAtIndex(0);

                    for (const StaticMeshSegment& meshSegment : lod.meshSegments) {
                        DrawCallDescription drawCall = meshSegment.drawCallDescription({ VertexComponent::Position3F }, scene);
                        drawCall.firstInstance = static_cast<uint32_t>(drawIdx++);
                        cmdList.issueDrawCall(drawCall);
                    }
                }
            }

            cmdList.endRendering();
            
            cmdList.textureWriteBarrier(indexTexture);
            cmdList.textureWriteBarrier(depthTexture);

            cmdList.setComputeState(collectState);
            cmdList.bindSet(collectIndexBindingSet, 0);

            vec2 pickLocation = input.mousePosition();
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

        uint32_t drawIdx = 0;
        for (StaticMeshInstance& instance : scene.scene().staticMeshInstances()) {
            if (const StaticMesh* staticMesh = scene.staticMeshForHandle(instance.mesh)) {

                // TODO: Pick LOD properly (i.e. the same as drawn in the main passes)
                const StaticMeshLOD& lod = staticMesh->lodAtIndex(0);

                for (const StaticMeshSegment& meshSegment : lod.meshSegments) {

                    if (drawIdx == selectedIdx) {
                        // TODO: This will break if/when we resize the instance vector
                        scene.scene().setSelectedInstance(&instance);
                        return;
                    }

                    drawIdx += 1;
                }
            }
        }

        // If no mesh was found, we must have clicked on the background so deselect current
        scene.scene().setSelectedInstance(nullptr);
    }

    if (deferredResult.specifyFocusDepth) {
        float focusDepth = pickingData.depth;
        Camera& camera = scene.camera();

        if (CameraController* cameraController = camera.controller()) {
            cameraController->setTargetFocusDepth(focusDepth);
        } else {
            camera.setFocusDepth(focusDepth);
        }
    }
}
