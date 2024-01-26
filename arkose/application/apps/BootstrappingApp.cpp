#include "BootstrappingApp.h"

class DrawTriangleNode final : public RenderPipelineNode {
    virtual std::string name() const override { return "Draw Triangle"; }
    virtual ExecuteCallback construct(GpuScene& scene, Registry& reg) override
    {
        Shader bootstrapShader = Shader::createBasicRasterize("d3d12-bootstrap/demo.hlsl",
                                                              "d3d12-bootstrap/demo.hlsl",
                                                              { ShaderDefine::makeBool("D3D12_SAMPLE_BASIC", true) });

        VertexLayout vertexLayout = { VertexComponent::Position3F, VertexComponent::TexCoord2F };
        RenderStateBuilder renderStateBuilder { reg.windowRenderTarget(), bootstrapShader, vertexLayout };
        RenderState& renderState = reg.createRenderState(renderStateBuilder);


        struct Vertex {
            vec3 position;
            vec2 uv;
        };

        // Create mesh buffers

        const Vertex vertices[4] = {
            // Upper left
            { { -0.5f, 0.5f, 0 }, { 0, 0 } },
            // Upper right
            { { 0.5f, 0.5f, 0 }, { 1, 0 } },
            // Bottom right
            { { 0.5f, -0.5f, 0 }, { 1, 1 } },
            // Bottom left
            { { -0.5f, -0.5f, 0 }, { 0, 1 } }
        };

        const int indices[6] = {
            0, 2, 1, 2, 0, 3
        };

        Buffer& vertexBuffer = reg.createBufferForData(vertices, Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        Buffer& indexBuffer = reg.createBufferForData(indices, Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);

        return [&](AppState const& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

            ClearValue clearValue;
            clearValue.color = ClearColor::srgbColor(0.2f, 0.2f, 0.2f, 1.0f);
            cmdList.beginRendering(renderState, clearValue, true);

            cmdList.bindVertexBuffer(vertexBuffer, sizeof(Vertex), 0);
            cmdList.bindIndexBuffer(indexBuffer, IndexType::UInt32);
            cmdList.drawIndexed(6, 0);

        };
    }
};

//

void BootstrappingApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    scene.setupFromDescription({ .path = "assets/sample/sponza.json",
                                 .withRayTracing = false,
                                 .withMeshShading = false });

    pipeline.addNode<DrawTriangleNode>();

}

bool BootstrappingApp::update(Scene&, float elapsedTime, float deltaTime)
{
    return true;
}
