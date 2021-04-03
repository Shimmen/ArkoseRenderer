#include "ShowcaseApp.h"

#include "rendering/nodes/AutoExposureNode.h"
#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/CullingNode.h"
#include "rendering/nodes/DiffuseGINode.h"
#include "rendering/nodes/DiffuseGIProbeDebug.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/GBufferNode.h"
#include "rendering/nodes/PrepassNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/SceneNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/Input.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

#include "utility/IESProfile.h"

void ShowcaseApp::setup(Scene& scene, RenderGraph& graph)
{
    SCOPED_PROFILE_ZONE();

    scene.loadFromFile("assets/sample/sponza.json");

    // TODO: Move to the scene json
    SpotLight& testSpot = scene.addLight(std::make_unique<SpotLight>(vec3(1.0f, 0.25f, 0.25f), 25.0f, "assets/sample/ies/three-lobe-umbrella.ies", vec3(0, 1, 0), vec3(1, -1, 1)));
    testSpot.setShadowMapSize({ 512, 512 });

    if (!scene.hasProbeGrid()) {
        scene.generateProbeGridFromBoundingBox();
    }

    graph.addNode<SceneNode>(scene);
    graph.addNode<GBufferNode>(scene);
    graph.addNode<PickingNode>(scene);

    graph.addNode<ShadowMapNode>(scene);
    graph.addNode<DiffuseGINode>(scene);
    graph.addNode<CullingNode>(scene);
    graph.addNode<PrepassNode>(scene);
    graph.addNode<ForwardRenderNode>(scene);
    graph.addNode<SkyViewNode>(scene);
    graph.addNode<BloomNode>(scene);

    graph.addNode<AutoExposureNode>(scene);
    graph.addNode<DiffuseGIProbeDebug>(scene);

    graph.addNode("final", [](Registry& reg) {
        // TODO: We should probably use compute for this now.. we don't require interpolation or any type of depth writing etc.
        std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
        Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        VertexLayout vertexLayout = VertexLayout { VertexComponent::Position2F };

#define USE_FXAA 1
#if USE_FXAA
        Texture& ldrTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA8);
        RenderTarget& ldrTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &ldrTexture } });
#else
        const RenderTarget& ldrTarget = reg.windowRenderTarget();
#endif

        BindingSet& tonemapBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, reg.getTexture("forward", "color").value(), ShaderBindingType::TextureSampler } });
        Shader tonemapShader = Shader::createBasicRasterize("final/showcase/tonemap.vert", "final/showcase/tonemap.frag");
        RenderStateBuilder tonemapStateBuilder { ldrTarget, tonemapShader, vertexLayout };
        tonemapStateBuilder.addBindingSet(tonemapBindingSet);
        tonemapStateBuilder.writeDepth = false;
        tonemapStateBuilder.testDepth = false;
        RenderState& tonemapRenderState = reg.createRenderState(tonemapStateBuilder);

#if USE_FXAA
        BindingSet& fxaaBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, &ldrTexture, ShaderBindingType::TextureSampler } });
        Shader fxaaShader = Shader::createBasicRasterize("final/showcase/anti-alias.vert", "final/showcase/anti-alias.frag");
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

                static float subpix = 0.75f;
                cmdList.pushConstant(ShaderStageFragment, subpix, sizeof(vec2));

                static float edgeThreshold = 0.166f;
                cmdList.pushConstant(ShaderStageFragment, edgeThreshold, sizeof(vec2) + sizeof(float));

                static float edgeThresholdMin = 0.0833f;
                cmdList.pushConstant(ShaderStageFragment, edgeThresholdMin, sizeof(vec2) + 2 * sizeof(float));

                if (ImGui::TreeNode("FXAA")) {
                    ImGui::SliderFloat("Sub-pixel AA", &subpix, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Edge threshold", &edgeThreshold, 0.063f, 0.333f, "%.3f");
                    ImGui::SliderFloat("Edge threshold min", &edgeThresholdMin, 0.0312f, 0.0833f, "%.4f");
                    ImGui::TreePop();
                }

                static float filmGrainGain = 0.035f;
                cmdList.pushConstant(ShaderStageFragment, filmGrainGain, sizeof(vec2) + 3 * sizeof(float));
                cmdList.pushConstant(ShaderStageFragment, appState.frameIndex(), sizeof(vec2) + 4 * sizeof(float));

                if (ImGui::TreeNode("Film grain")) {
                    ImGui::SliderFloat("Grain gain", &filmGrainGain, 0.0f, 1.0f);
                    ImGui::TreePop();
                }
            }
            cmdList.draw(vertexBuffer, 3);
            cmdList.endRendering();
#endif
        };
    });
}
