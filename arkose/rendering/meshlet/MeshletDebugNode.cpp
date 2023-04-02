#include "MeshletDebugNode.h"

#include "rendering/GpuScene.h"
#include "rendering/util/ScopedDebugZone.h"
#include <ark/random.h>

RenderPipelineNode::ExecuteCallback MeshletDebugNode::construct(GpuScene& scene, Registry& reg)
{
    //MeshletCuller::CullData const& cullData = m_meshletCuller.construct(scene, reg);

    Texture& debugTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA8);
    reg.publish("MeshletDebugVis", debugTexture);

    Texture& depthTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::Depth32F);
    RenderTarget& meshletDebugRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &debugTexture },
                                                                      { RenderTarget::AttachmentType::Depth, &depthTexture } });

    MeshShaderPathParams const& meshShaderPathParams = createMeshShaderPath(scene, reg, meshletDebugRenderTarget);

    Shader drawIndexShader = Shader::createBasicRasterize("meshlet/meshletVisualizeSimple.vert", "meshlet/meshletVisualizeSimple.frag");
    RenderStateBuilder renderStateBuilder(meshletDebugRenderTarget, drawIndexShader, VertexLayout { VertexComponent::Position3F });
    renderStateBuilder.cullBackfaces = false;
    renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneCameraSet"));
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    MeshletManager const& meshletManager = scene.meshletManager();
    Buffer const& meshletPositionsBuffer = meshletManager.meshletPositionDataVertexBuffer();
    Buffer const& meshletIndexBuffer = meshletManager.meshletIndexBuffer();

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        executeMeshShaderPath(meshShaderPathParams, scene, cmdList, uploadBuffer);
        return;

        geometry::Frustum const& cameraFrustum = scene.camera().frustum();

        // TODO: Replace the naive code below with the culler + a single draw call!
        //       Or, at least offer this as the non-naive solution. The big cpu-bound
        //       draw call loop below can be very helpful for validating results, but
        //       it's incredibly inefficient.
        //m_meshletCuller.execute(cmdList, scene, cullData);

        // Keep meshlet colors consistent
        ark::Random rng { 12345 };

        cmdList.beginRendering(renderState, ClearValue::blackAtMaxDepth());
        cmdList.bindVertexBuffer(meshletPositionsBuffer);
        cmdList.bindIndexBuffer(meshletIndexBuffer, IndexType::UInt32);

        // NOTE: This is obviously not optimal... just for testing!
        std::vector<ShaderMeshlet> const& meshlets = scene.meshletManager().meshlets();
        for (auto const& instance : scene.staticMeshInstances()) {

            cmdList.setNamedUniform("worldFromLocal", instance->transform().worldMatrix());

            StaticMesh const& staticMesh = *scene.staticMeshForInstance(*instance);
            StaticMeshLOD const& staticMeshLod = staticMesh.lodAtIndex(0);
            for (StaticMeshSegment const& segment : staticMeshLod.meshSegments) {
                if (segment.meshletView.has_value()) {

                    MeshletView const& meshletView = segment.meshletView.value();
                    for (u32 meshletIdx = meshletView.firstMeshlet; meshletIdx < meshletView.firstMeshlet + meshletView.meshletCount; ++meshletIdx) {

                        // Generate color before culling, so the colors stay constant as the camera moves
                        vec3 color { rng.randomFloatInRange(0.0f, 1.0f),
                                     rng.randomFloatInRange(0.0f, 1.0f),
                                     rng.randomFloatInRange(0.0f, 1.0f) };

                        ShaderMeshlet const& meshlet = meshlets[meshletIdx];

                        auto meshletSphereBounds = geometry::Sphere(meshlet.center, meshlet.radius).transformed(instance->transform().worldMatrix());
                        if (cameraFrustum.includesSphere(meshletSphereBounds)) {

                            cmdList.setNamedUniform("meshletColor", color);
                            cmdList.issueDrawCall(DrawCallDescription { .type = DrawCallDescription::Type::Indexed,
                                                                        .firstIndex = meshlet.firstIndex,
                                                                        .indexCount = 3 * meshlet.triangleCount,
                                                                        .indexType = IndexType::UInt32 });
                        }
                    }
                }
            }
        }

        cmdList.endRendering();
    };
}

MeshletDebugNode::MeshShaderPathParams const& MeshletDebugNode::createMeshShaderPath(GpuScene& scene, Registry& reg, RenderTarget& renderTarget)
{
    MeshShaderPathParams& params = reg.allocate<MeshShaderPathParams>();
    params.cameraBindingSet = reg.getBindingSet("SceneCameraSet");

    constexpr u32 count = 14096;

    params.taskShaderCmdsBuffer = &reg.createBuffer(count * sizeof(vec4), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
    params.taskShaderCountBuffer = &reg.createBuffer(count * sizeof(u32), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
    params.drawableLookupBuffer = &reg.createBuffer(count * sizeof(u32), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    params.meshletTaskSetupBindingSet = &reg.createBindingSet({ ShaderBinding::storageBuffer(*reg.getBuffer("SceneObjectData")),
                                                                ShaderBinding::storageBuffer(*params.taskShaderCmdsBuffer),
                                                                ShaderBinding::storageBuffer(*params.taskShaderCountBuffer),
                                                                ShaderBinding::storageBuffer(*params.drawableLookupBuffer) });

    auto meshletTaskSetupDefines = { ShaderDefine::makeInt("GROUP_SIZE", params.groupSize) };
    Shader meshletTaskSetupShader = Shader::createCompute("meshlet/meshletTaskSetup.comp", meshletTaskSetupDefines);
    params.meshletTaskSetupState = &reg.createComputeState(meshletTaskSetupShader, { params.cameraBindingSet, params.meshletTaskSetupBindingSet });

    auto meshletDefines = { ShaderDefine::makeInt("GROUP_SIZE", params.groupSize),
                            ShaderDefine::makeInt("MAX_VERTEX_COUNT", 64), // TODO: Get these values from the driver preferences!
                            ShaderDefine::makeInt("MAX_PRIMITIVE_COUNT", 126) }; // TODO: Get these values from the driver preferences!
    Shader meshletShader = Shader::createMeshShading("meshlet/meshletVisualize.task", "meshlet/meshletVisualize.mesh", "meshlet/meshletVisualize.frag", meshletDefines);

    MeshletManager const& meshletManager = scene.meshletManager();
    params.meshShaderBindingSet = &reg.createBindingSet({ ShaderBinding::storageBufferReadonly(*params.drawableLookupBuffer),
                                                          ShaderBinding::storageBufferReadonly(*reg.getBuffer("SceneObjectData")),
                                                          ShaderBinding::storageBufferReadonly(meshletManager.meshletBuffer()),
                                                          ShaderBinding::storageBufferReadonly(meshletManager.meshletPositionDataVertexBuffer()),
                                                          ShaderBinding::storageBufferReadonly(meshletManager.meshletIndexBuffer()) });

    RenderStateBuilder renderStateBuilder(renderTarget, meshletShader, VertexLayout { VertexComponent::Position3F });
    renderStateBuilder.cullBackfaces = false;
    renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneCameraSet"));
    renderStateBuilder.stateBindings().at(1, *params.meshShaderBindingSet);
    params.meshShaderRenderState = &reg.createRenderState(renderStateBuilder);

    return params;
}

void MeshletDebugNode::executeMeshShaderPath(MeshShaderPathParams const& params, GpuScene& scene, CommandList& cmdList, UploadBuffer& uploadBuffer) const
{
    {
        ScopedDebugZone zone { cmdList, "Meshlet task setup" };

        cmdList.setComputeState(*params.meshletTaskSetupState);
        cmdList.bindSet(*params.cameraBindingSet, 0);
        cmdList.bindSet(*params.meshletTaskSetupBindingSet, 1);

        cmdList.fillBuffer(*params.taskShaderCountBuffer, 0);
        cmdList.bufferWriteBarrier({ params.taskShaderCountBuffer });

        u32 drawableCount = scene.drawableCountForFrame();
        cmdList.setNamedUniform("drawableCount", drawableCount);
        cmdList.dispatch({ drawableCount, 1, 1 }, { params.groupSize, 1, 1 });
    }

    cmdList.bufferWriteBarrier({ params.taskShaderCmdsBuffer, params.taskShaderCountBuffer, params.drawableLookupBuffer });

    {
        ScopedDebugZone zone { cmdList, "Mesh shader pipeline" };

        cmdList.beginRendering(*params.meshShaderRenderState, ClearValue::blackAtMaxDepth());

        constexpr u32 indirectDataStride = sizeof(ark::uvec4);
        constexpr u32 indirectDataOffset = 0;//sizeof(ark::uvec4);
        constexpr u32 indirectCountOffset = 0;

        cmdList.drawMeshTasksIndirect(*params.taskShaderCmdsBuffer, indirectDataStride, indirectDataOffset,
                                      *params.taskShaderCountBuffer, indirectCountOffset);
                                      //*params.taskShaderCmdsBuffer, indirectCountOffset);

        /*
        for (auto const& instance : scene.staticMeshInstances()) {

            StaticMesh const& staticMesh = *scene.staticMeshForInstance(*instance);
            StaticMeshLOD const& staticMeshLod = staticMesh.lodAtIndex(0);

            for (u32 segmentIdx = 0; segmentIdx < staticMeshLod.meshSegments.size(); ++segmentIdx) {

                u32 drawableHandleIdx = instance->drawableHandleForSegmentIndex(segmentIdx).indexOfType<u32>();
                cmdList.setNamedUniform("testDrawIdx", drawableHandleIdx);

                StaticMeshSegment const& segment = staticMeshLod.meshSegments[segmentIdx];
                if (segment.meshletView.has_value()) {
                    cmdList.drawMeshTasks(ark::divideAndRoundUp(segment.meshletView->meshletCount, params.groupSize), 1, 1);
                }
            }
        }
        */

        cmdList.endRendering();
    }
}
