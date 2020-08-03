#include "MultisampleTest.h"

#include "rendering/nodes/DebugForwardNode.h"
#include "rendering/nodes/GBufferNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/SceneNode.h"
#include "rendering/nodes/ShadowMapNode.h"
#include "rendering/nodes/SlowForwardRenderNode.h"
#include "rendering/scene/models/GltfModel.h"
#include "utility/GlobalState.h"
#include "utility/Input.h"
#include "utility/Logging.h"
#include <imgui.h>

std::vector<Backend::Capability> MultisampleTest::requiredCapabilities()
{
    return {};
}

std::vector<Backend::Capability> MultisampleTest::optionalCapabilities()
{
    return {};
}

void MultisampleTest::setup(RenderGraph& graph)
{
    scene().loadFromFile("assets/sample/sponza.json");

    graph.addNode<SceneNode>(scene());
    graph.addNode<PickingNode>(scene());
    graph.addNode<DebugForwardNode>(scene());

    graph.addNode("final", [](Registry& reg) {
        std::vector<vec2> fullScreenTriangle { { -1, -3 }, { -1, 1 }, { 3, 1 } };
        Buffer& vertexBuffer = reg.createBuffer(std::move(fullScreenTriangle), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        VertexLayout vertexLayout = VertexLayout { sizeof(vec2), { { 0, VertexAttributeType::Float2, 0 } } };

        BindingSet& sourceBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, reg.getTexture("forward", "color").value(), ShaderBindingType::TextureSampler } });
        BindingSet& envBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, reg.getBuffer("scene", "camera") },
                                                           { 1, ShaderStageFragment, reg.getTexture("scene", "environmentMap").value_or(&reg.createPixelTexture(vec4(1), true)), ShaderBindingType::TextureSampler },
                                                           { 2, ShaderStageFragment, reg.getTexture("forward", "depth").value(), ShaderBindingType::TextureSampler },
                                                           { 3, ShaderStageFragment, reg.getBuffer("scene", "environmentData") } });

        Shader shader = Shader::createBasicRasterize("final/multisampled.vert", "final/multisampled.frag");
        RenderStateBuilder renderStateBuilder { reg.windowRenderTarget(), shader, vertexLayout };
        renderStateBuilder.addBindingSet(sourceBindingSet).addBindingSet(envBindingSet);
        renderStateBuilder.writeDepth = false;
        renderStateBuilder.testDepth = false;

        RenderState& renderState = reg.createRenderState(renderStateBuilder);

        return [&](const AppState& appState, CommandList& cmdList) {
            static float exposure = 0.45f;
            ImGui::SliderFloat("Exposure", &exposure, 0.01f, 10.0f, "%.3f", 3.0f);

            cmdList.beginRendering(renderState, ClearColor(0.5f, 0.1f, 0.5f), 1.0f);
            
            cmdList.bindSet(sourceBindingSet, 0);
            cmdList.bindSet(envBindingSet, 1);
            
            cmdList.pushConstant(ShaderStageFragment, exposure);

            constexpr int maxMultisampling = static_cast<int>(DebugForwardNode::multisamplingLevel());
            static int multisamplingLevel = maxMultisampling;

            ImGui::Text("Num samples of multisampling (in final)");
            ImGui::RadioButton("1X", &multisamplingLevel, 1);
            ImGui::SameLine();
            ImGui::RadioButton("2X", &multisamplingLevel, 2);
            ImGui::SameLine();
            ImGui::RadioButton("4X", &multisamplingLevel, 4);
            ImGui::SameLine();
            ImGui::RadioButton("8X", &multisamplingLevel, 8);
            MOOSLIB_ASSERT(multisamplingLevel > 1); // (since we use a sampler2DMS)
            cmdList.pushConstant(ShaderStageFragment, multisamplingLevel, 4);
            
            cmdList.draw(vertexBuffer, 3);
        };
    });
}

void MultisampleTest::update(float elapsedTime, float deltaTime)
{
    const Input& input = Input::instance();
    scene().camera().update(input, GlobalState::get().windowExtent(), deltaTime);
}
