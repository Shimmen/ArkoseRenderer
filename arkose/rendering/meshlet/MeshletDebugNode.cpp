#include "MeshletDebugNode.h"

#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include "rendering/util/ScopedDebugZone.h"
#include <ark/random.h>
#include <imgui.h>

void MeshletDebugNode::drawGui()
{
    ImGui::Text("Visualisation render path:");
    if (ImGui::RadioButton("Vertex shader", m_renderPath == RenderPath::VertexShader)) m_renderPath = RenderPath::VertexShader;
    if (ImGui::RadioButton("Mesh shader (direct)", m_renderPath == RenderPath::MeshShaderDirect)) m_renderPath = RenderPath::MeshShaderDirect;
    if (ImGui::RadioButton("Mesh shader (indirect)", m_renderPath == RenderPath::MeshShaderIndirect)) m_renderPath = RenderPath::MeshShaderIndirect;

    ImGui::Separator();

    ImGui::Checkbox("Frustum cull meshlets", &m_frustumCullMeshlets);
}

RenderPipelineNode::ExecuteCallback MeshletDebugNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& debugTexture = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::RGBA8);
    reg.publish("MeshletDebugVis", debugTexture);

    Texture& depthTexture = reg.createTexture2D(pipeline().renderResolution(), Texture::Format::Depth32F);
    RenderTarget& meshletDebugRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &debugTexture },
                                                                      { RenderTarget::AttachmentType::Depth, &depthTexture } });

    PassParams const& vertexShaderPathParams = createVertexShaderPath(scene, reg, meshletDebugRenderTarget);
    PassParams const& meshShaderPathDirectParams = createMeshShaderPath(scene, reg, meshletDebugRenderTarget, false);
    PassParams const& meshShaderPathIndirectParams = createMeshShaderPath(scene, reg, meshletDebugRenderTarget, true);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        switch (m_renderPath) {
        case RenderPath::VertexShader:
            executeVertexShaderPath(vertexShaderPathParams, scene, cmdList, uploadBuffer);
            break;
        case RenderPath::MeshShaderDirect:
            executeMeshShaderDirectPath(meshShaderPathDirectParams, scene, cmdList, uploadBuffer);
            break;
        case RenderPath::MeshShaderIndirect:
            executeMeshShaderIndirectPath(meshShaderPathIndirectParams, scene, cmdList, uploadBuffer);
            break;
        }
    };
}

MeshletDebugNode::PassParams const& MeshletDebugNode::createVertexShaderPath(GpuScene& scene, Registry& reg, RenderTarget& renderTarget)
{
    BindingSet& meshletDataBindingSet = reg.createBindingSet({ ShaderBinding::storageBufferReadonly(scene.vertexManager().meshletVertexIndirectionBuffer()),
                                                               ShaderBinding::storageBufferReadonly(scene.vertexManager().positionVertexBuffer()) });

    Shader drawIndexShader = Shader::createBasicRasterize("meshlet/meshletVisualize.vert", "meshlet/meshletVisualize.frag");
    RenderStateBuilder renderStateBuilder(renderTarget, drawIndexShader, VertexLayout {});
    renderStateBuilder.cullBackfaces = false;
    renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneCameraSet"));
    renderStateBuilder.stateBindings().at(1, meshletDataBindingSet);

    auto& params = reg.allocate<PassParams>();
    params.renderState = &reg.createRenderState(renderStateBuilder);

    return params;
}

MeshletDebugNode::PassParams const& MeshletDebugNode::createMeshShaderPath(GpuScene& scene, Registry& reg, RenderTarget& renderTarget, bool indirect)
{
    PassParams& params = reg.allocate<PassParams>();

    DrawKey catchAllDrawKeyMask {};
    constexpr u32 maxMeshletCount = 20'000;
    MeshletIndirectBuffer& indirectDataBuffer = m_meshletIndirectHelper.createIndirectBuffer(reg, catchAllDrawKeyMask, maxMeshletCount);
    params.meshletIndirectSetupState = &m_meshletIndirectHelper.createMeshletIndirectSetupState(reg, { &indirectDataBuffer });

    auto meshletDefines = { ShaderDefine::makeInt("INDIRECT", indirect),
                            ShaderDefine::makeInt("GROUP_SIZE", params.groupSize),
                            ShaderDefine::makeInt("MAX_VERTEX_COUNT", 64), // TODO: Get these values from the driver preferences!
                            ShaderDefine::makeInt("MAX_PRIMITIVE_COUNT", 126) }; // TODO: Get these values from the driver preferences!
                            
    Shader meshletShader = Shader::createMeshShading("meshlet/meshletVisualize.task", "meshlet/meshletVisualize.mesh", "meshlet/meshletVisualize.frag", meshletDefines);

    VertexManager const& vertexManager = scene.vertexManager();
    BindingSet& meshShaderBindingSet = reg.createBindingSet({ ShaderBinding::storageBufferReadonly(*indirectDataBuffer.buffer),
                                                              ShaderBinding::storageBufferReadonly(*reg.getBuffer("SceneObjectData")),
                                                              ShaderBinding::storageBufferReadonly(vertexManager.meshletBuffer()),
                                                              ShaderBinding::storageBufferReadonly(vertexManager.meshletIndexBuffer()),
                                                              ShaderBinding::storageBufferReadonly(vertexManager.meshletVertexIndirectionBuffer()),
                                                              ShaderBinding::storageBufferReadonly(vertexManager.positionVertexBuffer()) });

    RenderStateBuilder renderStateBuilder(renderTarget, meshletShader, VertexLayout { VertexComponent::Position3F });
    renderStateBuilder.cullBackfaces = false;
    renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneCameraSet"));
    renderStateBuilder.stateBindings().at(1, meshShaderBindingSet);
    params.renderState = &reg.createRenderState(renderStateBuilder);

    return params;
}

void MeshletDebugNode::executeVertexShaderPath(PassParams const& params, GpuScene& scene, CommandList& cmdList, UploadBuffer& uploadBuffer) const
{
    VertexManager const& vertexManager = scene.vertexManager();
    Buffer const& meshletIndexBuffer = vertexManager.meshletIndexBuffer();

    geometry::Frustum const& cameraFrustum = scene.camera().frustum();

    // Keep meshlet colors consistent
    ark::Random rng { 12345 };

    cmdList.beginRendering(*params.renderState, ClearValue::blackAtMaxDepth());
    cmdList.bindIndexBuffer(meshletIndexBuffer, vertexManager.meshletIndexType());

    // NOTE: This is obviously not optimal... just for testing!
    std::vector<ShaderMeshlet> const& meshlets = vertexManager.meshlets();
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
}

void MeshletDebugNode::executeMeshShaderDirectPath(PassParams const& params, GpuScene& scene, CommandList& cmdList, UploadBuffer& uploadBuffer) const
{
    cmdList.beginRendering(*params.renderState, ClearValue::blackAtMaxDepth());

    cmdList.setNamedUniform("frustumCull", m_frustumCullMeshlets);

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

    cmdList.endRendering();
}

void MeshletDebugNode::executeMeshShaderIndirectPath(PassParams const& params, GpuScene& scene, CommandList& cmdList, UploadBuffer& uploadBuffer) const
{
    m_meshletIndirectHelper.executeMeshletIndirectSetup(scene, cmdList, uploadBuffer, *params.meshletIndirectSetupState, {});

    cmdList.beginRendering(*params.renderState, ClearValue::blackAtMaxDepth());
    cmdList.setNamedUniform("frustumCull", m_frustumCullMeshlets);

    // NOTE: We only use the first and only indirect buffer for this meshlet debug view
    ARKOSE_ASSERT(params.meshletIndirectSetupState->indirectBuffers.size() == 1);
    MeshletIndirectBuffer const& indirectBuffer = *params.meshletIndirectSetupState->indirectBuffers[0];

    m_meshletIndirectHelper.drawMeshletsWithIndirectBuffer(cmdList, indirectBuffer);

    cmdList.endRendering();
}
