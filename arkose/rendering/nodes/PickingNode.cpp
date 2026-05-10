#include "PickingNode.h"

#include "system/Input.h"
#include "rendering/GpuScene.h"
#include "rendering/HairMesh.h"
#include "rendering/RenderPipeline.h"
#include "rendering/StaticMesh.h"
#include "scene/HairInstance.h"
#include "scene/MeshInstance.h"
#include "scene/editor/EditorScene.h"
#include "scene/camera/CameraController.h"
#include <ark/vector.h>

// Shader headers
#include "shaders/shared/PickingData.h"

RenderPipelineNode::ExecuteCallback PickingNode::construct(GpuScene& scene, Registry& reg)
{
    Buffer& resultBuffer = reg.createBuffer(sizeof(PickingData), Buffer::Usage::Readback);
    resultBuffer.setStride(sizeof(PickingData));

    Texture& indexTexture = reg.createTexture2D(pipeline().outputResolution(), Texture::Format::R32Uint);
    Texture& depthTexture = reg.createTexture2D(pipeline().outputResolution(), Texture::Format::Depth32F);

    RenderTarget& indexMapRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &indexTexture, LoadOp::Load, StoreOp::Store },
                                                                  { RenderTarget::AttachmentType::Depth, &depthTexture, LoadOp::Load, StoreOp::Store } });

    Shader drawIndexShader = Shader::createBasicRasterize("picking/drawIndices.vert", "picking/drawIndices.frag");
    VertexLayout positionOnlyLayout { VertexComponent::Position3F };

    RenderStateBuilder meshStateBuilder { indexMapRenderTarget, drawIndexShader, positionOnlyLayout };
    meshStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneObjectSet"));
    RenderState& meshDrawState = reg.createRenderState(meshStateBuilder);

    RenderStateBuilder hairStateBuilder { indexMapRenderTarget, drawIndexShader, positionOnlyLayout };
    hairStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneObjectSet"));
    hairStateBuilder.primitiveType = PrimitiveType::LineStrip;
    hairStateBuilder.enablePrimitiveRestart = true;
    RenderState& hairDrawState = reg.createRenderState(hairStateBuilder);

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
        bool meshSelectPick = !input.isGuiUsingMouse() && input.didClickButton(Button::Left);
        bool focusDepthPick = !input.isGuiUsingMouse() && input.didClickButton(Button::Middle);

        if (!meshSelectPick && !focusDepthPick) {
            return;
        }

        EditorScene& editorScene = scene.scene().editorScene();

        if (EditorGizmo* gizmo = editorScene.raycastScreenPointAgainstEditorGizmos(pickLocation)) {
            if (meshSelectPick) {
                editorScene.setSelectedObject(gizmo->editorObject());
            } else if (focusDepthPick) {
                setFocusDepth(scene, gizmo->distanceFromCamera());
            }
            return;
        }

        cmdList.clearTexture(indexTexture, ClearValue { .color = ClearColor::dataValues(-1.0f, 0.0f, 0.0f, 0.0f) });
        cmdList.clearTexture(depthTexture, ClearValue { .depth = 1.0f });

        cmdList.beginRendering(meshDrawState);
        cmdList.setNamedUniform("projectionFromWorld", scene.camera().viewProjectionMatrix());

        cmdList.bindVertexBuffer(scene.vertexManager().positionVertexBuffer(), scene.vertexManager().positionVertexLayout().packedVertexSize(), 0);
        cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

        for (auto& instance : scene.staticMeshInstances()) {
            StaticMesh const* staticMesh = scene.staticMeshForHandle(instance->mesh());
            if (staticMesh == nullptr) {
                continue;
            }

            // TODO: Pick LOD properly (i.e. the same as drawn in the main passes)
            StaticMeshLOD const& lod = staticMesh->lodAtIndex(0);

            for (size_t segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
                if (!instance->hasDrawableHandleForSegmentIndex(segmentIdx)) {
                    continue;
                }

                StaticMeshSegment const& meshSegment = lod.meshSegments[segmentIdx];
                DrawCallDescription drawCall = DrawCallDescription::fromVertexAllocation(meshSegment.vertexAllocation);
                drawCall.firstInstance = instance->drawableHandleForSegmentIndex(segmentIdx).indexOfType<u32>();
                cmdList.issueDrawCall(drawCall);
            }
        }

        for (auto& instance : scene.skeletalMeshInstances()) {
            size_t segmentCount = instance->skinningVertexMappings().size();
            for (size_t segmentIdx = 0; segmentIdx < segmentCount; ++segmentIdx) {
                if (!instance->hasDrawableHandleForSegmentIndex(segmentIdx)) {
                    continue;
                }

                SkinningVertexMapping const& mapping = instance->skinningVertexMappingForSegmentIndex(segmentIdx);
                DrawCallDescription drawCall = DrawCallDescription::fromVertexAllocation(mapping.skinnedTarget);
                drawCall.firstInstance = instance->drawableHandleForSegmentIndex(segmentIdx).indexOfType<u32>();
                cmdList.issueDrawCall(drawCall);
            }
        }

        cmdList.endRendering();

        if (!scene.hairInstances().empty()) {
            cmdList.beginRendering(hairDrawState);
            cmdList.setNamedUniform("projectionFromWorld", scene.camera().viewProjectionMatrix());

            cmdList.bindVertexBuffer(scene.vertexManager().hairPositionVertexBuffer(), scene.vertexManager().hairPositionVertexLayout().packedVertexSize(), 0);
            cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

            for (auto& hairInstance : scene.hairInstances()) {
                if (!hairInstance->drawableHandle()) {
                    continue;
                }

                HairMesh const* hairMesh = scene.hairMeshForHandle(hairInstance->hair());
                if (hairMesh == nullptr || !hairMesh->valid()) {
                    continue;
                }

                DrawCallDescription drawCall = hairMesh->drawCallDescription();
                drawCall.firstInstance = hairInstance->drawableHandle().indexOfType<u32>();
                cmdList.issueDrawCall(drawCall);
            }

            cmdList.endRendering();
        }

        cmdList.textureWriteBarrier(indexTexture);
        cmdList.textureWriteBarrier(depthTexture);

        cmdList.setComputeState(collectState);
        cmdList.setNamedUniform("mousePosition", pickLocation);
        cmdList.dispatch(indexTexture.extent(), { 16, 16, 1 });

        m_pendingDeferredResult = DeferredResult { .resultBuffer = &resultBuffer,
                                                   .selectMesh = meshSelectPick,
                                                   .specifyFocusDepth = focusDepthPick };
    };
}

void PickingNode::processDeferredResult(CommandList& cmdList, GpuScene& scene, const DeferredResult& deferredResult)
{
    // At least one must be specified
    ARKOSE_ASSERT(deferredResult.selectMesh || deferredResult.specifyFocusDepth);

    PickingData pickingData;
    cmdList.slowBlockingReadFromBuffer(*deferredResult.resultBuffer, 0, sizeof(pickingData), &pickingData);

    if (deferredResult.selectMesh) {
        EditorScene& editorScene = scene.scene().editorScene();

        DrawableObjectHandle pickedHandle = DrawableObjectHandle(pickingData.drawableIdx);
        if (IEditorObject* pickedObject = scene.editorObjectForDrawableHandle(pickedHandle)) {
            editorScene.setSelectedObject(*pickedObject);
        } else {
            // If no drawable object was found, we must have clicked on the background, so deselect current.
            editorScene.clearSelectedObject();
        }
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
