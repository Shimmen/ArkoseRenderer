#include "RayTracingApp.h"

#include "rendering/nodes/AutoExposureNode.h"
#include "rendering/nodes/ForwardRenderNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/RTAccelerationStructures.h"
#include "rendering/nodes/RTAmbientOcclusion.h"
#include "rendering/nodes/RTDiffuseGINode.h"
#include "rendering/nodes/RTFirstHitNode.h"
#include "rendering/nodes/RTReflectionsNode.h"
#include "rendering/nodes/SceneNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/Input.h"
#include <imgui.h>
#include <moos/transform.h>

std::vector<Backend::Capability> RayTracingApp::requiredCapabilities()
{
    return { Backend::Capability::RtxRayTracing };
}

std::vector<Backend::Capability> RayTracingApp::optionalCapabilities()
{
    return {};
}

void RayTracingApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    //scene().loadFromFile("assets/sample/sponza.json");
    scene.loadFromFile("assets/sample/cornell-box.json");

    bool rtxOn = true;
    //bool firstHit = true;

    pipeline.addNode<SceneNode>(scene);
    pipeline.addNode<PickingNode>(scene);
    pipeline.addNode<ShadowMapNode>(scene);
    pipeline.addNode<ForwardRenderNode>(scene);
    if (rtxOn) {
        pipeline.addNode<RTAccelerationStructures>(scene);
        pipeline.addNode<RTAmbientOcclusion>(scene);
        pipeline.addNode<RTReflectionsNode>(scene);
        pipeline.addNode<RTDiffuseGINode>(scene);
        //if (firstHit) {
        //    pipeline.addNode<RTFirstHitNode>(scene());
        //}
    }

    pipeline.addNode<SkyViewNode>(scene);

    pipeline.addNode("rt-combine", [](Registry& reg) {
        Texture* targetTexture = reg.getTexture("SceneColor");
        if (!targetTexture)
            targetTexture = &reg.loadTexture2D("assets/test-pattern.png", true, true);

        BindingSet& targetBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, targetTexture, ShaderBindingType::StorageImage } });

        Texture* diffuseGI = reg.getTexture("rtDiffuseGI");
        if (!diffuseGI)
            diffuseGI = &reg.createPixelTexture(vec4(0, 0, 0, 1), true);

        Texture* ambientOcclusion = reg.getTexture("rtAO");
        if (!ambientOcclusion)
            ambientOcclusion = &reg.createPixelTexture(vec4(1, 1, 1, 1), true);

        BindingSet& giBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, diffuseGI, ShaderBindingType::TextureSampler },
                                                          { 1, ShaderStageCompute, ambientOcclusion, ShaderBindingType::TextureSampler } });

        Shader shader = Shader::createCompute("post/gi-combine.comp");
        ComputeState& computeState = reg.createComputeState(shader, { &targetBindingSet, &giBindingSet });

        return [&](const AppState& appState, CommandList& cmdList) {
            cmdList.setComputeState(computeState);
            cmdList.bindSet(targetBindingSet, 0);
            cmdList.bindSet(giBindingSet, 1);

            static bool includeDiffuseGI = true;
            ImGui::Checkbox("Include diffuse GI", &includeDiffuseGI);
            cmdList.pushConstant(ShaderStageCompute, includeDiffuseGI);

            cmdList.dispatch(appState.windowExtent(), { 16, 16, 1 });
        };
    });

    pipeline.addNode<AutoExposureNode>(scene);

    pipeline.addNode("final", [](Registry& reg) {
        // TODO: We should probably use compute for this now.. we don't require interpolation or any type of depth writing etc.
        std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
        Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        VertexLayout vertexLayout = VertexLayout { VertexComponent::Position2F };

        BindingSet& bindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, reg.getTexture("SceneColor"), ShaderBindingType::TextureSampler } });
        Shader shader = Shader::createBasicRasterize("final/showcase/tonemap.vert", "final/showcase/tonemap.frag");
        RenderStateBuilder renderStateBuilder { reg.windowRenderTarget(), shader, vertexLayout };
        renderStateBuilder.stateBindings().at(0, bindingSet);
        renderStateBuilder.writeDepth = false;
        renderStateBuilder.testDepth = false;

        RenderState& renderState = reg.createRenderState(renderStateBuilder);

        return [&](const AppState& appState, CommandList& cmdList) {
            cmdList.beginRendering(renderState, ClearColor::srgbColor(0.5f, 0.1f, 0.5f), 1.0f);
            cmdList.draw(vertexBuffer, 3);

            if (ImGui::Button("Take screenshot")) {
                static int imageIdx = 0;
                const Texture& finalColor = *reg.windowRenderTarget().attachment(RenderTarget::AttachmentType::Color0);
                cmdList.saveTextureToFile(finalColor, "assets/screenshot_" + std::to_string(imageIdx++) + ".png");
            }
        };
    });
}

void RayTracingApp::update(Scene& scene, float elapsedTime, float deltaTime)
{
    m_frameTimeAvg.report(deltaTime);

    ImGui::Begin("RayTracingApp");
    float avgFrameTime = m_frameTimeAvg.runningAverage() * 1000.0f;
    ImGui::Text("Frame time: %.2f ms/frame", avgFrameTime);
    ImGui::End();
}
