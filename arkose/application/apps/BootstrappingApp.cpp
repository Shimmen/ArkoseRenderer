#include "BootstrappingApp.h"

#include "core/CommandLine.h"

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
        Shader bootstrapShader = Shader::createBasicRasterize("d3d12-bootstrap/demo.vert",
                                                              "d3d12-bootstrap/demo.frag");

        Buffer& constantBuffer = reg.createBuffer(sizeof(m_scale), Buffer::Usage::ConstantBuffer);
        constantBuffer.setName("DemoConstantBuffer");

        ImageAsset* testImage = ImageAsset::loadOrCreate("assets/engine/default/test-pattern.png");
        Texture& testTexture = reg.createTexture({ .extent = { testImage->width(), testImage->height(), 1 },
                                                   .format = Texture::convertImageFormatToTextureFormat(testImage->format(), ImageType::sRGBColor),
                                                   .filter = Texture::Filters::linear(),
                                                   .wrapMode = ImageWrapModes::clampAllToEdge(),
                                                   .mipmap = Texture::Mipmap::None });
        testTexture.setData(testImage->pixelDataForMip(0).data(), testImage->pixelDataForMip(0).size(), 0, 0);
        testTexture.setName("DemoTestTexture");
        m_texture = &testTexture;

        RenderTarget& outputRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, reg.outputTexture() } });

        VertexLayout vertexLayout = { VertexComponent::Position3F, VertexComponent::TexCoord2F };
        RenderStateBuilder renderStateBuilder { outputRenderTarget, bootstrapShader, vertexLayout };

        BindingSet& bindingSet = reg.createBindingSet({ ShaderBinding::constantBuffer(constantBuffer, ShaderStage::Vertex),
                                                        ShaderBinding::sampledTexture(testTexture, ShaderStage::Fragment) });
        renderStateBuilder.stateBindings().at(0, bindingSet);

        RenderState& renderState = reg.createRenderState(renderStateBuilder);
        renderState.setName("DemoRenderState");

        struct Vertex {
            vec3 position;
            vec2 uv;
        };

        // Create mesh buffers

        Vertex vertices[4] = {
            // Upper left
            { { -0.5f, -0.5f, 0 }, { 0, 0 } },
            // Upper right
            { { 0.5f, -0.5f, 0 }, { 1, 0 } },
            // Bottom right
            { { 0.5f, 0.5f, 0 }, { 1, 1 } },
            // Bottom left
            { { -0.5f, 0.5f, 0 }, { 0, 1 } }
        };

        // HACK: Figure out how we actually want to handle these cases! In most cases we just use different
        // backend-specific projections, but when we truly want to draw a screen space quad it'd be nice to
        // actually have a way to handle this case.
        if (CommandLine::hasArgument("-d3d12")) {
            for (Vertex& vertex : vertices) { 
                vertex.position.y *= -1.0f;
            }
        }

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

void BootstrappingApp::setup(Backend& graphicsBackend, PhysicsBackend* physicsBackend)
{
    AppBase::setup(graphicsBackend, physicsBackend);

    scene().setupFromDescription({ .withRayTracing = false,
                                 .withMeshShading = false });

    mainRenderPipeline().addNode<DrawTriangleNode>();

}

bool BootstrappingApp::update(float elapsedTime, float deltaTime)
{
    return AppBase::update(elapsedTime, deltaTime);
}

void BootstrappingApp::render(Backend& graphicsBackend, float elapsedTime, float deltaTime)
{
    AppBase::render(graphicsBackend, elapsedTime, deltaTime);
}
