#include "MeshletForwardRenderNode.h"

#include "rendering/GpuScene.h"
#include "rendering/util/BlendModeUtil.h"
#include <imgui.h>

void MeshletForwardRenderNode::drawGui()
{
    ImGui::Checkbox("Frustum cull instances", &m_frustumCullInstances);
    ImGui::Checkbox("Frustum cull meshlets", &m_frustumCullMeshlets);
}

RenderPipelineNode::ExecuteCallback MeshletForwardRenderNode::construct(GpuScene& scene, Registry& reg)
{
    std::vector<RenderStateWithIndirectData*> const& renderStates = createRenderStates(reg, scene);

    // TODO: If we collect render states and indirect buffers into separate arrays we won't have to do this...
    // However, it potentially make other code more messy, so perhaps not worth doing.
    std::vector<MeshletIndirectBuffer*> indirectBuffers {};
    for (auto const& renderState : renderStates) { indirectBuffers.push_back(renderState->indirectBuffer); }
    MeshletIndirectSetupState const& indirectSetupState = m_meshletIndirectHelper.createMeshletIndirectSetupState(reg, indirectBuffers);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        MeshletIndirectSetupOptions setupOptions { .frustumCullInstances = m_frustumCullInstances };
        m_meshletIndirectHelper.executeMeshletIndirectSetup(scene, cmdList, uploadBuffer, indirectSetupState, setupOptions);

        for (RenderStateWithIndirectData* renderState : renderStates) {

            // NOTE: If render target is not set up to clear then the clear value specified here is arbitrary
            cmdList.beginRendering(*renderState->renderState, ClearValue::blackAtMaxDepth());

            cmdList.setNamedUniform("ambientAmount", scene.preExposedAmbient());
            cmdList.setNamedUniform("frustumJitterCorrection", scene.camera().frustumJitterUVCorrection());
            cmdList.setNamedUniform("invTargetSize", renderState->renderState->renderTarget().extent().inverse());

            cmdList.setNamedUniform("frustumCullMeshlets", m_frustumCullMeshlets);

            MeshletIndirectBuffer& indirectBuffer = *renderState->indirectBuffer;
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

MeshletForwardRenderNode::RenderStateWithIndirectData& MeshletForwardRenderNode::makeRenderState(Registry& reg, GpuScene const& scene, PassSettings passSettings) const
{
    auto shaderDefines = { // Forward rendering specific
                           ShaderDefine::makeInt("FORWARD_BLEND_MODE", blendModeToShaderBlendMode(passSettings.drawKeyMask.blendMode().value())),
                           ShaderDefine::makeBool("FORWARD_DOUBLE_SIDED", passSettings.drawKeyMask.doubleSided().value()),
                           ShaderDefine::makeBool("FORWARD_MESH_SHADING", true),

                           // Mesh shading specific
                           ShaderDefine::makeInt("GROUP_SIZE", 32), // TODO: Get these values from the driver preferences!
                           ShaderDefine::makeInt("MAX_VERTEX_COUNT", 64), // TODO: Get these values from the driver preferences!
                           ShaderDefine::makeInt("MAX_PRIMITIVE_COUNT", 126) }; // TODO: Get these values from the driver preferences!

    Shader shader = Shader::createMeshShading("meshlet/meshletForward.task",
                                              "meshlet/meshletForward.mesh",
                                              "forward/forward.frag",
                                              shaderDefines);

    LoadOp loadOp = passSettings.firstPass ? LoadOp::Clear : LoadOp::Load;
    RenderStateBuilder renderStateBuilder { makeRenderTarget(reg, loadOp), shader, {} };
    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.cullBackfaces = !passSettings.drawKeyMask.doubleSided().value(); // TODO: We probably want to use dynamic state for double sided!

    // If we have a previous prepass ignore non-written stencil pixels. We always have to write something to the
    // stencil buffer, however, as the sky view shader relies on this test when drawing. Write bit1 for skin BRDF.
    renderStateBuilder.stencilMode = reg.hasPreviousNode("Prepass") ? StencilMode::ReplaceIfGreaterOrEqual : StencilMode::AlwaysWrite;
    renderStateBuilder.stencilValue = 0x01;
    if (passSettings.drawKeyMask.brdf().value() == Brdf::Skin) {
        renderStateBuilder.stencilValue |= 0x03;
    }

    Texture* dirLightProjectedShadow = reg.getTexture("DirectionalLightProjectedShadow");
    Texture* sphereLightProjectedShadow = reg.getTexture("SphereLightProjectedShadow");
    Texture* localLightShadowMapAtlas = reg.getTexture("LocalLightShadowMapAtlas");
    Buffer* localLightShadowAllocations = reg.getBuffer("LocalLightShadowAllocations");

    // Allow running without shadows
    if (!dirLightProjectedShadow || !sphereLightProjectedShadow || !localLightShadowMapAtlas || !localLightShadowAllocations) {
        Texture& placeholderTex = reg.createPixelTexture(vec4(1.0f), false);
        Buffer& placeholderBuffer = reg.createBufferForData(std::vector<int>(0), Buffer::Usage::StorageBuffer);
        dirLightProjectedShadow = dirLightProjectedShadow ? dirLightProjectedShadow : &placeholderTex;
        sphereLightProjectedShadow = sphereLightProjectedShadow ? sphereLightProjectedShadow : &placeholderTex;
        localLightShadowMapAtlas = localLightShadowMapAtlas ? localLightShadowMapAtlas : &placeholderTex;
        localLightShadowAllocations = localLightShadowAllocations ? localLightShadowAllocations : &placeholderBuffer;
    }

    BindingSet& shadowBindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*dirLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*sphereLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*localLightShadowMapAtlas),
                                                          ShaderBinding::storageBuffer(*localLightShadowAllocations) });

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
    bindings.at(3, scene.globalMaterialBindingSet());
    bindings.at(4, *reg.getBindingSet("SceneLightSet"));
    bindings.at(5, shadowBindingSet);

    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName(passSettings.debugName);

    auto& renderStateWithIndirectData = reg.allocate<RenderStateWithIndirectData>();
    renderStateWithIndirectData.renderState = &renderState;
    renderStateWithIndirectData.indirectBuffer = &indirectBuffer;
    return renderStateWithIndirectData;
}

std::vector<MeshletForwardRenderNode::RenderStateWithIndirectData*>& MeshletForwardRenderNode::createRenderStates(Registry& reg, GpuScene const& scene)
{
    std::vector<PassSettings> passes {};
    std::string debugName = "Meshlet";

    // TODO: It's not correct to ignore explicit velocity, but we're
    // not ready to support this quite yet, so for now it will have to do.
    auto explicitVelocityMask = std::optional<bool>();

    auto brdfs = { Brdf::Default, Brdf::Skin };
    for (Brdf brdf : brdfs) {

        debugName += BrdfName(brdf);

        passes.push_back({ .drawKeyMask = DrawKey(brdf, BlendMode::Opaque, false, explicitVelocityMask),
                           .maxMeshlets = 50'000,
                           .debugName = debugName + "Opaque" });

        passes.push_back({ .drawKeyMask = DrawKey(brdf, BlendMode::Opaque, true, explicitVelocityMask),
                           .maxMeshlets = 50'000,
                           .debugName = debugName + "OpaqueDoubleSided" });

        passes.push_back({ .drawKeyMask = DrawKey(brdf, BlendMode::Masked, false, explicitVelocityMask),
                           .maxMeshlets = 50'000,
                           .debugName = debugName + "Masked" });

        passes.push_back({ .drawKeyMask = DrawKey(brdf, BlendMode::Masked, true, explicitVelocityMask),
                           .maxMeshlets = 50'000,
                           .debugName = debugName + "MaskedDoubleSided" });
    }

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
