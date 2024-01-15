#include "BootstrappingApp.h"

class DrawTriangleNode final : public RenderPipelineNode {
    virtual std::string name() const override { return "Draw Triangle"; }
    virtual ExecuteCallback construct(GpuScene& scene, Registry& reg) override
    {
        // TODO: Hook up all the backend stuff so that this is actually called!

        struct Vertex {
            float position[3];
            float uv[2];
        };

        const Vertex vertices[4] = {
            // Upper Left
            { { -0.5f, 0.5f, 0 }, { 0, 0 } },
            // Upper Right
            { { 0.5f, 0.5f, 0 }, { 1, 0 } },
            // Bottom right
            { { 0.5f, -0.5f, 0 }, { 1, 1 } },
            // Bottom left
            { { -0.5f, -0.5f, 0 }, { 0, 1 } }
        };

        const int indices[6] = {
            0, 1, 2, 2, 3, 0
        };

        Buffer& vertexBuffer = reg.createBufferForData(vertices, Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
        Buffer& indexBuffer = reg.createBufferForData(vertices, Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);

        return [&](AppState const& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

            //cmdList.beginRendering()
            //cmdList.drawIndexed(vertexBuffer, indexBuffer, 6, IndexType::UInt32);
            //cmdList.endRendering();

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
