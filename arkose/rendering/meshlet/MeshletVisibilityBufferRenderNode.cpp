#include "MeshletVisibilityBufferRenderNode.h"

#include "rendering/GpuScene.h"
#include <imgui.h>

void MeshletVisibilityBufferRenderNode::drawGui()
{
    ImGui::Checkbox("Frustum cull instances", &m_frustumCullInstances);
    ImGui::Checkbox("Frustum cull meshlets", &m_frustumCullMeshlets);
}

RenderPipelineNode::ExecuteCallback MeshletVisibilityBufferRenderNode::construct(GpuScene& scene, Registry& reg)
{
    if (reg.hasPreviousNode("Prepass")) {
        ARKOSE_LOG(Warning, "Using visibility buffer rendering when Prepass node is in the render pipeline, this is probably not intended!");
    }

    Texture::Description visibilityDataTexDesc { .extent = reg.windowRenderTarget().extent(),
                                                 .format = Texture::Format::R32Uint };
    
    m_instanceVisibilityTexture = &reg.createTexture(visibilityDataTexDesc);
    reg.publish("InstanceVisibilityTexture", *m_instanceVisibilityTexture);

    m_triangleVisibilityTexture = &reg.createTexture(visibilityDataTexDesc);
    reg.publish("TriangleVisibilityTexture", *m_triangleVisibilityTexture);

    std::vector<RenderStateWithIndirectData*> const& renderStates = createRenderStates(reg, scene);

    // TODO: If we collect render states and indirect buffers into separate arrays we won't have to do this...
    // However, it potentially make other code more messy, so perhaps not worth doing.
    std::vector<MeshletIndirectBuffer*> indirectBuffers {};
    for (auto const& renderState : renderStates) {
        indirectBuffers.push_back(renderState->indirectBuffer);
    }
    MeshletIndirectSetupState const& indirectSetupState = m_meshletIndirectHelper.createMeshletIndirectSetupState(reg, indirectBuffers);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        MeshletIndirectSetupOptions setupOptions { .frustumCullInstances = m_frustumCullInstances };
        m_meshletIndirectHelper.executeMeshletIndirectSetup(scene, cmdList, uploadBuffer, indirectSetupState, setupOptions);

        for (RenderStateWithIndirectData* renderState : renderStates) {

            // NOTE: If render target is not set up to clear then the clear value specified here is arbitrary
            cmdList.beginRendering(*renderState->renderState, ClearValue::blackAtMaxDepth());

            cmdList.setNamedUniform("frustumCullMeshlets", m_frustumCullMeshlets);

            MeshletIndirectBuffer& indirectBuffer = *renderState->indirectBuffer;
            m_meshletIndirectHelper.drawMeshletsWithIndirectBuffer(cmdList, indirectBuffer);

            cmdList.endRendering();
        }
    };
}

RenderTarget& MeshletVisibilityBufferRenderNode::makeRenderTarget(Registry& reg, LoadOp loadOp) const
{
    Texture& depthTexture = *reg.getTexture("SceneDepth");
    return reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, m_instanceVisibilityTexture, loadOp, StoreOp::Store },
                                    { RenderTarget::AttachmentType::Color1, m_triangleVisibilityTexture, loadOp, StoreOp::Store },
                                    { RenderTarget::AttachmentType::Depth, &depthTexture, loadOp, StoreOp::Store } });
}

MeshletVisibilityBufferRenderNode::RenderStateWithIndirectData& MeshletVisibilityBufferRenderNode::makeRenderState(Registry& reg, GpuScene const& scene, PassSettings passSettings) const
{
    BlendMode blendMode = passSettings.drawKeyMask.blendMode().value();
    ARKOSE_ASSERT(blendMode == BlendMode::Opaque || blendMode == BlendMode::Masked);

    // TODO: Get these values from the driver preferences!
    i32 groupSize = 32;
    i32 maxVertexCount = 64;
    i32 maxPrimitiveCount = 126;

    auto shaderDefines = { ShaderDefine::makeInt("VISBUF_BLEND_MODE", blendModeToShaderBlendMode(blendMode)),
                           ShaderDefine::makeInt("GROUP_SIZE", groupSize),
                           ShaderDefine::makeInt("MAX_VERTEX_COUNT", maxVertexCount),
                           ShaderDefine::makeInt("MAX_PRIMITIVE_COUNT", maxPrimitiveCount) };

    Shader shader = Shader::createMeshShading("meshlet/meshletVisibilityBuffer.task",
                                              "meshlet/meshletVisibilityBuffer.mesh",
                                              "meshlet/meshletVisibilityBuffer.frag",
                                              shaderDefines);

    LoadOp loadOp = passSettings.firstPass ? LoadOp::Clear : LoadOp::Load;
    RenderStateBuilder renderStateBuilder { makeRenderTarget(reg, loadOp), shader, {} };
    renderStateBuilder.cullBackfaces = !passSettings.drawKeyMask.doubleSided().value(); // TODO: We probably want to use dynamic state for double sided!
    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.stencilMode = StencilMode::AlwaysWrite; // for sky view

    MeshletIndirectBuffer& indirectBuffer = m_meshletIndirectHelper.createIndirectBuffer(reg, passSettings.drawKeyMask, passSettings.maxMeshlets);

    MeshletManager const& meshletManager = scene.meshletManager();

    BindingSet& taskShaderBindingSet = reg.createBindingSet({ ShaderBinding::storageBufferReadonly(*indirectBuffer.buffer),
                                                              ShaderBinding::storageBufferReadonly(*reg.getBuffer("SceneObjectData")),
                                                              ShaderBinding::storageBufferReadonly(meshletManager.meshletBuffer()) });

    BindingSet& meshShaderBindingSet = reg.createBindingSet({ ShaderBinding::storageBufferReadonly(meshletManager.meshletIndexBuffer()),
                                                              ShaderBinding::storageBufferReadonly(meshletManager.meshletPositionDataVertexBuffer()),
                                                              ShaderBinding::storageBufferReadonly(meshletManager.meshletNonPositionDataVertexBuffer()) });

    StateBindings& bindings = renderStateBuilder.stateBindings();
    bindings.at(0, *reg.getBindingSet("SceneCameraSet"));
    bindings.at(1, taskShaderBindingSet);
    bindings.at(2, meshShaderBindingSet);

    // For masked, need to read the mask texture to determine if pixel should be masked or not
    if (blendMode == BlendMode::Masked) {
        bindings.at(3, scene.globalMaterialBindingSet());
    }

    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName(passSettings.debugName);

    auto& renderStateWithIndirectData = reg.allocate<RenderStateWithIndirectData>();
    renderStateWithIndirectData.renderState = &renderState;
    renderStateWithIndirectData.indirectBuffer = &indirectBuffer;
    return renderStateWithIndirectData;
}

std::vector<MeshletVisibilityBufferRenderNode::RenderStateWithIndirectData*>& MeshletVisibilityBufferRenderNode::createRenderStates(Registry& reg, GpuScene const& scene) const
{
    std::vector<PassSettings> passes {};
    std::string debugName = "MeshletVisibility";

    // NOTE: We don't discriminate between BRDFs, include all in the same draw call
    auto brdfMask = std::optional<Brdf>();

    passes.push_back({ .drawKeyMask = DrawKey(brdfMask, BlendMode::Opaque, false),
                        .maxMeshlets = 50'000,
                        .debugName = debugName + "Opaque" });

    passes.push_back({ .drawKeyMask = DrawKey(brdfMask, BlendMode::Opaque, true),
                        .maxMeshlets = 50'000,
                        .debugName = debugName + "OpaqueDoubleSided" });

    passes.push_back({ .drawKeyMask = DrawKey(brdfMask, BlendMode::Masked, false),
                        .maxMeshlets = 50'000,
                        .debugName = debugName + "Masked" });

    passes.push_back({ .drawKeyMask = DrawKey(brdfMask, BlendMode::Masked, true),
                        .maxMeshlets = 50'000,
                        .debugName = debugName + "MaskedDoubleSided" });

    // Ensure that the first pass is marked as such, so we know to clear the render targets before starting the pass
    if (passes.size() > 0) {
        passes.front().firstPass = true;
    }

    auto& renderStates = reg.allocate<std::vector<RenderStateWithIndirectData*>>();
    for (PassSettings const& pass : passes) {
        renderStates.push_back(&makeRenderState(reg, scene, pass));
    }

    return renderStates;
}