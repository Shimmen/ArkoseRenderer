#include "ForwardRenderNode.h"

#include "core/Types.h"
#include "rendering/GpuScene.h"
#include "rendering/util/ScopedDebugZone.h"
#include "utility/Profiling.h"
#include <imgui.h>

// Shared shader headers
#include "shaders/shared/IndirectData.h"
#include "shaders/shared/LightData.h"

RenderPipelineNode::ExecuteCallback ForwardRenderNode::construct(GpuScene& scene, Registry& reg)
{
    // TODO: Improve the way culling is handled so we don't have to special-case these so much.
    // It's okay now, but when we have multiple materials/shaders doing this would be a big pain.

    RenderState& renderStateOpaque = makeRenderState(reg, scene, ForwardPass::Opaque);
    Buffer& opaqueDrawCmdsBuffer = *reg.getBuffer("MainViewOpaqueDrawCmds");
    Buffer& opaqueDrawCountBuffer = *reg.getBuffer("MainViewOpaqueDrawCount");

    RenderState& renderStateMasked = makeRenderState(reg, scene, ForwardPass::Masked);
    Buffer& maskedDrawCmdsBuffer = *reg.getBuffer("MainViewMaskedDrawCmds");
    Buffer& maskedDrawCountBuffer = *reg.getBuffer("MainViewMaskedDrawCount");

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        scene.ensureDrawCallIsAvailableForAll(m_vertexLayout);

        auto setCommonNamedUniforms = [&](const RenderState& renderState) {
            cmdList.setNamedUniform("ambientAmount", scene.preExposedAmbient());
            cmdList.setNamedUniform("frustumJitterCorrection", scene.camera().frustumJitterUVCorrection());
            cmdList.setNamedUniform("invTargetSize", renderState.renderTarget().extent().inverse());
        };

        cmdList.bindVertexBuffer(scene.globalVertexBufferForLayout(m_vertexLayout));
        cmdList.bindIndexBuffer(scene.globalIndexBuffer(), scene.globalIndexBufferType());

        {
            ScopedDebugZone zone { cmdList, "Opaque" };
            cmdList.beginRendering(renderStateOpaque, ClearValue::blackAtMaxDepth());
            setCommonNamedUniforms(renderStateOpaque);
            cmdList.drawIndirect(opaqueDrawCmdsBuffer, opaqueDrawCountBuffer);
            cmdList.endRendering();
        }

        {
            ScopedDebugZone zone { cmdList, "Masked" };
            cmdList.beginRendering(renderStateMasked);
            setCommonNamedUniforms(renderStateMasked);
            cmdList.drawIndirect(maskedDrawCmdsBuffer, maskedDrawCountBuffer);
            cmdList.endRendering();
        }
    };
}

RenderTarget& ForwardRenderNode::makeRenderTarget(Registry& reg, LoadOp loadOp) const
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

RenderState& ForwardRenderNode::makeRenderState(Registry& reg, const GpuScene& scene, ForwardPass forwardPass) const
{
    BindingSet* drawablesBindingSet = nullptr;
    const char* stateName = nullptr;
    int blendModeInt = 0;
    LoadOp loadOp;

    switch (forwardPass) {
    case ForwardPass::Opaque:
        drawablesBindingSet = reg.getBindingSet("MainViewCulledDrawablesOpaqueSet");
        stateName = "ForwardOpaque";
        blendModeInt = BLEND_MODE_OPAQUE;
        loadOp = LoadOp::Clear;
        
        break;
    case ForwardPass::Masked:
        drawablesBindingSet = reg.getBindingSet("MainViewCulledDrawablesMaskedSet");
        stateName = "ForwardMasked";
        blendModeInt = BLEND_MODE_MASKED;
        loadOp = LoadOp::Load;
        break;
    }

    std::vector<ShaderDefine> shaderDefines {};

    ARKOSE_ASSERT(blendModeInt != 0);
    shaderDefines.push_back(ShaderDefine::makeInt("FORWARD_BLEND_MODE", blendModeInt));

    Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag", shaderDefines);

    RenderStateBuilder renderStateBuilder { makeRenderTarget(reg, loadOp), shader, m_vertexLayout };
    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    
    renderStateBuilder.stencilMode = StencilMode::AlwaysWrite;
    if (forwardPass == ForwardPass::Opaque && reg.hasPreviousNode("Prepass")) {
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

    StateBindings& bindings = renderStateBuilder.stateBindings();
    bindings.at(0, *reg.getBindingSet("SceneCameraSet"));
    bindings.at(1, scene.globalMaterialBindingSet());
    bindings.at(2, *reg.getBindingSet("SceneLightSet"));
    bindings.at(3, *drawablesBindingSet);
    bindings.at(4, shadowBindingSet);

    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName(stateName);

    return renderState;
}
