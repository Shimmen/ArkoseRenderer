#include "ShowcaseApp.h"

#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/DiffuseGINode.h"
#include "rendering/nodes/ExposureNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/GBufferNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/SceneNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/GlobalState.h"
#include "utility/Input.h"
#include "utility/Logging.h"
#include <imgui.h>

std::vector<Backend::Capability> ShowcaseApp::requiredCapabilities()
{
    return { Backend::Capability::ShaderTextureArrayDynamicIndexing,
             Backend::Capability::ShaderBufferArrayDynamicIndexing };
}

std::vector<Backend::Capability> ShowcaseApp::optionalCapabilities()
{
    return {};
}

void ShowcaseApp::setup(RenderGraph& graph)
{
    LogInfo("Loading scene\n");
    scene().loadFromFile("assets/sample/cornell-box.json");
    LogInfo("Done loading scene\n");

    LogInfo("Setting up render graph\n");

    graph.addNode<SceneNode>(scene());
    graph.addNode<PickingNode>(scene());

    graph.addNode<ShadowMapNode>(scene());

    graph.addNode<GBufferNode>(scene());
    graph.addNode<ForwardRenderNode>(scene());
    graph.addNode<SkyViewNode>(scene());

    auto probeGridDescription = DiffuseGINode::ProbeGridDescription {
        .gridDimensions = { 8, 8, 8 },
        .probeSpacing = { 1, 1, 1 },
        .offsetToFirst = vec3(-4, -2, -4)
    };
    graph.addNode<DiffuseGINode>(scene(), probeGridDescription);

    graph.addNode<BloomNode>(scene());

    graph.addNode<ExposureNode>(scene());

    graph.addNode("Final", [](Registry& reg) {
        // TODO: We should probably use compute for this now.. we don't require interpolation or any type of depth writing etc.
        std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
        Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        VertexLayout vertexLayout = VertexLayout { sizeof(vec2), { { 0, VertexAttributeType::Float2, 0 } } };

#define USE_FXAA 1
#if USE_FXAA
        Texture& ldrTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA8);
        RenderTarget& ldrTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &ldrTexture } });
#else
        const RenderTarget& ldrTarget = reg.windowRenderTarget();
#endif

        BindingSet& tonemapBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, reg.getTexture("forward", "color").value(), ShaderBindingType::TextureSampler } });
        Shader tonemapShader = Shader::createBasicRasterize("final/simple.vert", "final/simple.frag");
        RenderStateBuilder tonemapStateBuilder { ldrTarget, tonemapShader, vertexLayout };
        tonemapStateBuilder.addBindingSet(tonemapBindingSet);
        tonemapStateBuilder.writeDepth = false;
        tonemapStateBuilder.testDepth = false;
        RenderState& tonemapRenderState = reg.createRenderState(tonemapStateBuilder);

#if USE_FXAA
        BindingSet& fxaaBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, &ldrTexture, ShaderBindingType::TextureSampler } });
        Shader fxaaShader = Shader::createBasicRasterize("fxaa/fxaa.vert", "fxaa/fxaa.frag");
        RenderStateBuilder fxaaStateBuilder { reg.windowRenderTarget(), fxaaShader, vertexLayout };
        fxaaStateBuilder.addBindingSet(fxaaBindingSet);
        fxaaStateBuilder.writeDepth = false;
        fxaaStateBuilder.testDepth = false;
        RenderState& fxaaRenderState = reg.createRenderState(fxaaStateBuilder);
#endif

        return [&](const AppState& appState, CommandList& cmdList) {
            cmdList.beginRendering(tonemapRenderState, ClearColor(0.5f, 0.1f, 0.5f), 1.0f);
            cmdList.bindSet(tonemapBindingSet, 0);
            cmdList.draw(vertexBuffer, 3);
            cmdList.endRendering();

#if USE_FXAA
            cmdList.beginRendering(fxaaRenderState, ClearColor(0.5f, 0.1f, 0.5f), 1.0f);
            cmdList.bindSet(fxaaBindingSet, 0);
            {
                vec2 pixelSize = vec2(1.0f / ldrTexture.extent().width(), 1.0f / ldrTexture.extent().height());
                cmdList.pushConstant(ShaderStageFragment, pixelSize, 0);

                // TODO: Add UI!
                static float subpix = 0.75f;
                cmdList.pushConstant(ShaderStageFragment, subpix, sizeof(vec2));

                // TODO: Add UI!
                static float edgeThreshold = 0.166f;
                cmdList.pushConstant(ShaderStageFragment, edgeThreshold, sizeof(vec2) + sizeof(float));

                // TODO: Add UI!
                static float edgeThresholdMin = 0.0833f;
                cmdList.pushConstant(ShaderStageFragment, edgeThresholdMin, sizeof(vec2) + 2 * sizeof(float));
            }
            cmdList.draw(vertexBuffer, 3);
            cmdList.endRendering();
#endif
        };
    });

    LogInfo("Done setting up render graph\n");
}

void ShowcaseApp::update(float elapsedTime, float deltaTime)
{
    const Input& input = Input::instance();
    scene().camera().update(input, GlobalState::get().windowExtent(), deltaTime);
}