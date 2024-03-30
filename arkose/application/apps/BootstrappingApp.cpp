#include "BootstrappingApp.h"

class DrawTriangleNode final : public RenderPipelineNode {
    virtual std::string name() const override { return "Draw Triangle"; }

    virtual void drawGui() override
    {
        ImGui::SliderFloat2("Scale", &m_scale.x, 0.01f, 1.99f);

        if (m_texture) {
            drawTextureVisualizeGui(*m_texture);
        }
    }

    virtual ExecuteCallback construct(GpuScene& scene, Registry& reg) override
    {
        Shader bootstrapShader = Shader::createBasicRasterize("d3d12-bootstrap/demo.hlsl",
                                                              "d3d12-bootstrap/demo.hlsl",
                                                              { ShaderDefine::makeBool("D3D12_SAMPLE_CONSTANT_BUFFER", true) });

        Buffer& constantBuffer = reg.createBuffer(sizeof(m_scale), Buffer::Usage::ConstantBuffer);
        constantBuffer.setName("DemoConstantBuffer");

        ImageAsset* testImage = ImageAsset::loadOrCreate("assets/test-pattern.png");
        Texture& testTexture = reg.createTexture({ .extent = { testImage->width(), testImage->height(), 1 },
                                                   .format = Texture::convertImageFormatToTextureFormat(testImage->format(), ImageType::sRGBColor),
                                                   .filter = Texture::Filters::linear(),
                                                   .wrapMode = ImageWrapModes::clampAllToEdge(),
                                                   .mipmap = Texture::Mipmap::Linear });
        testTexture.setData(testImage->pixelDataForMip(0).data(), testImage->pixelDataForMip(0).size(), 0, 0);
        testTexture.setName("DemoTestTexture");
        m_texture = &testTexture;

        VertexLayout vertexLayout = { VertexComponent::Position3F, VertexComponent::TexCoord2F };
        RenderStateBuilder renderStateBuilder { reg.windowRenderTarget(), bootstrapShader, vertexLayout };

        BindingSet& bindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(constantBuffer, ShaderStage::Vertex) });
        renderStateBuilder.stateBindings().at(0, bindingSet);

        RenderState& renderState = reg.createRenderState(renderStateBuilder);
        renderState.setName("DemoRenderState");

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

        Buffer& vertexBuffer = reg.createBufferForData(vertices, Buffer::Usage::Vertex);
        vertexBuffer.setName("DemoVertexBuffer");

        Buffer& indexBuffer = reg.createBufferForData(indices, Buffer::Usage::Index);
        indexBuffer.setName("DemoIndexBuffer");

        return [&](AppState const& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

            uploadBuffer.upload(m_scale, constantBuffer);
            cmdList.executeBufferCopyOperations(uploadBuffer.popPendingOperations());

            ClearValue clearValue;
            clearValue.color = ClearColor::srgbColor(0.5f, 0.5f, 0.5f, 1.0f);
            cmdList.beginRendering(renderState, clearValue, true);

            cmdList.bindVertexBuffer(vertexBuffer, sizeof(Vertex), 0);
            cmdList.bindIndexBuffer(indexBuffer, IndexType::UInt32);
            cmdList.drawIndexed(6, 0);

        };
    }

private:
    vec4 m_scale { 1.0f };
    Texture* m_texture {};
};

//

void BootstrappingApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    m_pipeline = &pipeline;

    scene.setupFromDescription({ .path = "assets/sample/sponza.json",
                                 .withRayTracing = false,
                                 .withMeshShading = false });

    pipeline.addNode<DrawTriangleNode>();

}

bool BootstrappingApp::update(Scene&, float elapsedTime, float deltaTime)
{
    m_pipeline->drawGui();
    return true;
}
