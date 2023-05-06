#include "MeshletForwardRenderNode.h"

#include "rendering/GpuScene.h"
#include <imgui.h>

void MeshletForwardRenderNode::drawGui()
{
    ImGui::Checkbox("Frustum cull instances", &m_frustumCullInstances);
    ImGui::Checkbox("Frustum cull meshlets", &m_frustumCullMeshlets);
}

RenderPipelineNode::ExecuteCallback MeshletForwardRenderNode::construct(GpuScene& scene, Registry& reg)
{
    PassSettings opaqueSettings { .debugName = "MeshletForwardOpaque",
                                  .maxMeshlets = 20'000,
                                  .blendMode = PassBlendMode::Opaque,
                                  .doubleSided = false /* TODO : We probably want to use dynamic state for double sided ! */ };
    RenderStateWithIndirectData& renderStateOpaque = makeRenderState(reg, scene, opaqueSettings);

    // TODO: Use more indirect buffers for the different passes (per BRDF * double sided * ..?)
    //  The shader needs to know which instances to assign to which indirect data buffers.
    //  One way is to associate a "sort key" for each indirect buffer and pass that along.
    //  I.e., we specify what sort key each buffer is for, and each drawable has its own
    //  sort key part of its data, so we simply do the culling then put in respective buffer.

    MeshletIndirectSetupState const& indirectSetupState = m_meshletIndirectHelper.createMeshletIndirectSetupState(reg, { renderStateOpaque.indirectDataBuffer });

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        MeshletIndirectSetupOptions setupOptions { .frustumCullInstances = m_frustumCullInstances };
        m_meshletIndirectHelper.executeMeshletIndirectSetup(scene, cmdList, uploadBuffer, indirectSetupState, setupOptions);

        std::vector<RenderStateWithIndirectData*> renderStates = { &renderStateOpaque };
        for (RenderStateWithIndirectData* renderState : renderStates) {

            cmdList.beginRendering(*renderState->renderState, ClearValue::blackAtMaxDepth());

            cmdList.setNamedUniform("ambientAmount", scene.preExposedAmbient());
            cmdList.setNamedUniform("frustumJitterCorrection", scene.camera().frustumJitterUVCorrection());
            cmdList.setNamedUniform("invTargetSize", renderState->renderState->renderTarget().extent().inverse());

            cmdList.setNamedUniform("frustumCullMeshlets", m_frustumCullMeshlets);

            Buffer& indirectBuffer = *renderState->indirectDataBuffer;
            m_meshletIndirectHelper.drawMeshletsWithIndirectBuffer(cmdList, indirectBuffer);

            cmdList.endRendering();

        }
    };
}

RenderTarget& MeshletForwardRenderNode::makeRenderTarget(Registry& reg, LoadOp loadOp) const
{
    Texture* colorTexture = reg.getTexture("SceneColor");
    Texture* normalVelocityTexture = reg.getTexture("SceneNormalVelocity");
    Texture* materialTexture = reg.getTexture("SceneMaterial");
    Texture* baseColorTexture = reg.getTexture("SceneBaseColor");
    Texture* depthTexture = reg.getTexture("SceneDepth");

    // For depth, if we have prepass we should never do any other load op than to load
    LoadOp depthLoadOp = reg.hasPreviousNode("Prepass") ? LoadOp::Load : loadOp;

    return reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, colorTexture, loadOp, StoreOp::Store },
                                    { RenderTarget::AttachmentType::Color1, normalVelocityTexture, loadOp, StoreOp::Store },
                                    { RenderTarget::AttachmentType::Color2, materialTexture, loadOp, StoreOp::Store },
                                    { RenderTarget::AttachmentType::Color3, baseColorTexture, loadOp, StoreOp::Store },
                                    { RenderTarget::AttachmentType::Depth, depthTexture, depthLoadOp, StoreOp::Store } });
}

MeshletForwardRenderNode::RenderStateWithIndirectData& MeshletForwardRenderNode::makeRenderState(Registry& reg, const GpuScene& scene, PassSettings passSettings) const
{
    int blendModeInt = 0;
    LoadOp loadOp;
    switch (passSettings.blendMode) {
    case PassBlendMode::Opaque:
        blendModeInt = BLEND_MODE_OPAQUE;
        loadOp = LoadOp::Clear;
        break;
    case PassBlendMode::Masked:
        blendModeInt = BLEND_MODE_MASKED;
        loadOp = LoadOp::Load;
        break;
    }

    ARKOSE_ASSERT(blendModeInt != 0);
    auto shaderDefines = { // Forward rendering specific
                           ShaderDefine::makeInt("FORWARD_BLEND_MODE", blendModeInt),
                           ShaderDefine::makeBool("FORWARD_DOUBLE_SIDED", passSettings.doubleSided),
                           ShaderDefine::makeBool("FORWARD_MESH_SHADING", true),

                           // Mesh shading specific
                           ShaderDefine::makeInt("GROUP_SIZE", 32), // TODO: Get these values from the driver preferences!
                           ShaderDefine::makeInt("MAX_VERTEX_COUNT", 64), // TODO: Get these values from the driver preferences!
                           ShaderDefine::makeInt("MAX_PRIMITIVE_COUNT", 126) }; // TODO: Get these values from the driver preferences!

    Shader shader = Shader::createMeshShading("meshlet/meshletForward.task",
                                              "meshlet/meshletForward.mesh",
                                              "forward/forward.frag",
                                              shaderDefines);

    RenderStateBuilder renderStateBuilder { makeRenderTarget(reg, loadOp), shader, {} };
    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.cullBackfaces = !passSettings.doubleSided;

    renderStateBuilder.stencilMode = StencilMode::AlwaysWrite;
    if (passSettings.blendMode == PassBlendMode::Opaque && reg.hasPreviousNode("Prepass")) {
        renderStateBuilder.stencilMode = StencilMode::PassIfNotZero;
    }

    Texture* dirLightProjectedShadow = reg.getTexture("DirectionalLightProjectedShadow");
    Texture* sphereLightProjectedShadow = reg.getTexture("SphereLightProjectedShadow");
    Texture* localLightShadowMapAtlas = reg.getTexture("LocalLightShadowMapAtlas");
    Buffer* localLightShadowAllocations = reg.getBuffer("LocalLightShadowAllocations");

    // Allow running without shadows
    if (!dirLightProjectedShadow || !sphereLightProjectedShadow || !localLightShadowMapAtlas || !localLightShadowAllocations) {
        Texture& placeholderTex = reg.createPixelTexture(vec4(1.0f), false);
        Buffer& placeholderBuffer = reg.createBufferForData(std::vector<int>(0), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
        dirLightProjectedShadow = dirLightProjectedShadow ? dirLightProjectedShadow : &placeholderTex;
        sphereLightProjectedShadow = sphereLightProjectedShadow ? sphereLightProjectedShadow : &placeholderTex;
        localLightShadowMapAtlas = localLightShadowMapAtlas ? localLightShadowMapAtlas : &placeholderTex;
        localLightShadowAllocations = localLightShadowAllocations ? localLightShadowAllocations : &placeholderBuffer;
    }

    BindingSet& shadowBindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*dirLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*sphereLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*localLightShadowMapAtlas),
                                                          ShaderBinding::storageBuffer(*localLightShadowAllocations) });

    Buffer& indirectDataBuffer = m_meshletIndirectHelper.createIndirectBuffer(reg, passSettings.maxMeshlets);

    MeshletManager const& meshletManager = scene.meshletManager();

    BindingSet& taskShaderBindingSet = reg.createBindingSet({ ShaderBinding::storageBufferReadonly(indirectDataBuffer),
                                                              ShaderBinding::storageBufferReadonly(*reg.getBuffer("SceneObjectData")),
                                                              ShaderBinding::storageBufferReadonly(meshletManager.meshletBuffer()) });

    BindingSet& meshShaderBindingSet = reg.createBindingSet({ ShaderBinding::storageBufferReadonly(meshletManager.meshletIndexBuffer()),
                                                              ShaderBinding::storageBufferReadonly(meshletManager.meshletPositionDataVertexBuffer()),
                                                              ShaderBinding::storageBufferReadonly(meshletManager.meshletNonPositionDataVertexBuffer()) });

    StateBindings& bindings = renderStateBuilder.stateBindings();
    bindings.at(0, *reg.getBindingSet("SceneCameraSet"));
    bindings.at(1, taskShaderBindingSet);
    bindings.at(2, meshShaderBindingSet);
    bindings.at(3, scene.globalMaterialBindingSet());
    bindings.at(4, *reg.getBindingSet("SceneLightSet"));
    bindings.at(5, shadowBindingSet);

    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName(passSettings.debugName);

    auto& renderStateWithIndirectData = reg.allocate<RenderStateWithIndirectData>();
    renderStateWithIndirectData.renderState = &renderState;
    renderStateWithIndirectData.indirectDataBuffer = &indirectDataBuffer;
    return renderStateWithIndirectData;
}
