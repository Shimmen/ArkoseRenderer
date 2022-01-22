#include "ForwardRenderNode.h"

#include "SceneNode.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

// Shared shader headers
using uint = uint32_t;
#include "IndirectData.h"
#include "LightData.h"

RenderPipelineNode::ExecuteCallback ForwardRenderNode::construct(Scene& scene, Registry& reg)
{
    ///////////////////////
    // constructNode
    BindingSet* ddgiSamplingBindingSet = nullptr;
    if (reg.hasPreviousNode("DDGI")) {
        ddgiSamplingBindingSet = reg.getBindingSet("DDGISamplingSet");
    }
    ///////////////////////

    Texture& colorTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("SceneColor", colorTexture);

    Texture& normalTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("SceneNormal", normalTexture);

    Texture& velocityTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("SceneVelocity", velocityTexture);

    Texture& baseColorTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA8);
    reg.publish("SceneBaseColor", baseColorTexture);

    Texture& diffueGiTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA16F);
    reg.publish("DiffuseGI", diffueGiTexture);

    if (!reg.hasPreviousNode("Prepass"))
        reg.publish("SceneDepth", reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::Depth24Stencil8, Texture::Filters::nearest()));
    Texture& depthTexture = *reg.getTexture("SceneDepth");

    auto makeRenderTarget = [&](LoadOp loadOp) -> RenderTarget& {
        // For depth, if we have prepass we should never do any other load op than to load
        LoadOp depthLoadOp = reg.hasPreviousNode("Prepass") ? LoadOp::Load : loadOp;
        return reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture, loadOp, StoreOp::Store },
                                        { RenderTarget::AttachmentType::Color1, &normalTexture, loadOp, StoreOp::Store },
                                        { RenderTarget::AttachmentType::Color2, &velocityTexture, loadOp, StoreOp::Store },
                                        { RenderTarget::AttachmentType::Color3, &baseColorTexture, loadOp, StoreOp::Store },
                                        { RenderTarget::AttachmentType::Color4, &diffueGiTexture, loadOp, StoreOp::Store },
                                        { RenderTarget::AttachmentType::Depth, &depthTexture, depthLoadOp, StoreOp::Store } });
    };

    BindingSet& cameraBindingSet = *reg.getBindingSet("SceneCameraSet");
    BindingSet& materialBindingSet = scene.globalMaterialBindingSet();
    BindingSet& lightBindingSet = *reg.getBindingSet("SceneLightSet");

    BindingSet& opaqueDrawablesBindingSet = *reg.getBindingSet("MainViewCulledDrawablesOpaqueSet");
    BindingSet& maskedDrawablesBindingSet = *reg.getBindingSet("MainViewCulledDrawablesMaskedSet");

    bool useDDGI = ddgiSamplingBindingSet != nullptr;
    auto includeDDGIDefine = ShaderDefine::makeBool("FORWARD_INCLUDE_DDGI", useDDGI);

    RenderState* renderStateOpaque;
    {
        auto blendModeDefine = ShaderDefine::makeInt("FORWARD_BLEND_MODE", BLEND_MODE_OPAQUE);
        Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag", { includeDDGIDefine, blendModeDefine });

        RenderStateBuilder renderStateBuilder { makeRenderTarget(LoadOp::Clear), shader, m_vertexLayout };
        renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
        renderStateBuilder.stencilMode = reg.hasPreviousNode("Prepass") ? StencilMode::PassIfNotZero : StencilMode::AlwaysWrite;
        renderStateBuilder.stateBindings().at(0, cameraBindingSet);
        renderStateBuilder.stateBindings().at(1, materialBindingSet);
        renderStateBuilder.stateBindings().at(2, lightBindingSet);
        renderStateBuilder.stateBindings().at(3, opaqueDrawablesBindingSet);
        if (useDDGI)
            renderStateBuilder.stateBindings().at(5, *ddgiSamplingBindingSet);

        renderStateOpaque = &reg.createRenderState(renderStateBuilder);
        renderStateOpaque->setName("ForwardOpaque");
    }

    RenderState* renderStateMasked;
    {
        auto blendModeDefine = ShaderDefine::makeInt("FORWARD_BLEND_MODE", BLEND_MODE_MASKED);
        Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag", { includeDDGIDefine, blendModeDefine });

        RenderStateBuilder renderStateBuilder { makeRenderTarget(LoadOp::Load), shader, m_vertexLayout };
        renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
        renderStateBuilder.stencilMode = StencilMode::AlwaysWrite;
        renderStateBuilder.stateBindings().at(0, cameraBindingSet);
        renderStateBuilder.stateBindings().at(1, materialBindingSet);
        renderStateBuilder.stateBindings().at(2, lightBindingSet);
        renderStateBuilder.stateBindings().at(3, maskedDrawablesBindingSet);
        if (useDDGI)
            renderStateBuilder.stateBindings().at(5, *ddgiSamplingBindingSet);

        renderStateMasked = &reg.createRenderState(renderStateBuilder);
        renderStateMasked->setName("ForwardMasked");
    }

    Buffer& opaqueDrawCmdsBuffer = *reg.getBuffer("MainViewOpaqueDrawCmds");
    Buffer& opaqueDrawCountBuffer = *reg.getBuffer("MainViewOpaqueDrawCount");

    Buffer& maskedDrawCmdsBuffer = *reg.getBuffer("MainViewMaskedDrawCmds");
    Buffer& maskedDrawCountBuffer = *reg.getBuffer("MainViewMaskedDrawCount");

    return [&, renderStateOpaque, renderStateMasked](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        scene.forEachMesh([&](size_t, Mesh& mesh) {
            mesh.ensureDrawCallIsAvailable(m_vertexLayout, scene);
        });

        auto setCommonNamedUniforms = [&]() {
            cmdList.setNamedUniform("ambientAmount", scene.exposedAmbient());
            cmdList.setNamedUniform("indirectExposure", scene.lightPreExposureValue());
            cmdList.setNamedUniform("totalFrustumJitter", scene.camera().totalFrustumJitterInUVCoords());
        };

        cmdList.bindVertexBuffer(scene.globalVertexBufferForLayout(m_vertexLayout));
        cmdList.bindIndexBuffer(scene.globalIndexBuffer(), scene.globalIndexBufferType());

        cmdList.beginDebugLabel("Opaque");
        cmdList.beginRendering(*renderStateOpaque, ClearColor::srgbColor(0, 0, 0, 0), 1.0f);
        {
            setCommonNamedUniforms();
            
            cmdList.drawIndirect(opaqueDrawCmdsBuffer, opaqueDrawCountBuffer);
        }
        cmdList.endRendering();
        cmdList.endDebugLabel();

        cmdList.beginDebugLabel("Masked");
        cmdList.beginRendering(*renderStateMasked);
        {
            setCommonNamedUniforms();
            cmdList.drawIndirect(maskedDrawCmdsBuffer, maskedDrawCountBuffer);
        }
        cmdList.endRendering();
        cmdList.endDebugLabel();
    };
}
