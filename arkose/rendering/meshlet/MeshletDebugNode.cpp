#include "MeshletDebugNode.h"

#include "rendering/GpuScene.h"
#include <ark/random.h>

RenderPipelineNode::ExecuteCallback MeshletDebugNode::construct(GpuScene& scene, Registry& reg)
{
    //MeshletCuller::CullData const& cullData = m_meshletCuller.construct(scene, reg);

    Texture& debugTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::RGBA8);
    reg.publish("MeshletDebugVis", debugTexture);

    Texture& depthTexture = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::Depth32F);
    RenderTarget& meshletDebugRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &debugTexture },
                                                                      { RenderTarget::AttachmentType::Depth, &depthTexture } });

    Shader drawIndexShader = Shader::createBasicRasterize("meshlet/meshletVisualizeSimple.vert", "meshlet/meshletVisualizeSimple.frag");
    RenderStateBuilder renderStateBuilder(meshletDebugRenderTarget, drawIndexShader, VertexLayout { VertexComponent::Position3F });
    renderStateBuilder.cullBackfaces = false;
    renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneCameraSet"));
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    MeshletManager const& meshletManager = scene.meshletManager();
    Buffer const& meshletPositionsBuffer = meshletManager.meshletPositionDataVertexBuffer();
    Buffer const& meshletIndexBuffer = meshletManager.meshletIndexBuffer();

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        geometry::Frustum const& cameraFrustum = scene.camera().frustum();

        // TODO: Replace the naive code below with the culler + a single draw call!
        //       Or, at least offer this as the non-naive solution. The big cpu-bound
        //       draw call loop below can be very helpful for validating results, but
        //       it's incredibly inefficient.
        //m_meshletCuller.execute(cmdList, scene, cullData);

        // Keep meshlet colors consistent
        ark::Random rng { 12345 };

        cmdList.beginRendering(renderState, ClearValue::blackAtMaxDepth());
        cmdList.bindVertexBuffer(meshletPositionsBuffer);
        cmdList.bindIndexBuffer(meshletIndexBuffer, IndexType::UInt32);

        // NOTE: This is obviously not optimal... just for testing!
        std::vector<ShaderMeshlet> const& meshlets = scene.meshletManager().meshlets();
        for (auto const& instance : scene.staticMeshInstances()) {

            cmdList.setNamedUniform("worldFromLocal", instance->transform().worldMatrix());

            StaticMesh const& staticMesh = *scene.staticMeshForInstance(*instance);
            StaticMeshLOD const& staticMeshLod = staticMesh.lodAtIndex(0);
            for (StaticMeshSegment const& segment : staticMeshLod.meshSegments) {
                if (segment.meshletView.has_value()) {

                    MeshletView const& meshletView = segment.meshletView.value();
                    for (u32 meshletIdx = meshletView.firstMeshlet; meshletIdx < meshletView.firstMeshlet + meshletView.meshletCount; ++meshletIdx) {

                        // Generate color before culling, so the colors stay constant as the camera moves
                        vec3 color { rng.randomFloatInRange(0.5f, 1.0f),
                                     rng.randomFloatInRange(0.5f, 1.0f),
                                     rng.randomFloatInRange(0.5f, 1.0f) };

                        ShaderMeshlet const& meshlet = meshlets[meshletIdx];

                        auto meshletSphereBounds = geometry::Sphere(meshlet.center, meshlet.radius).transformed(instance->transform().worldMatrix());
                        if (cameraFrustum.includesSphere(meshletSphereBounds)) {

                            cmdList.setNamedUniform("meshletColor", color);
                            cmdList.issueDrawCall(DrawCallDescription { .type = DrawCallDescription::Type::Indexed,
                                                                        .firstIndex = meshlet.firstIndex,
                                                                        .indexCount = 3 * meshlet.triangleCount,
                                                                        .indexType = IndexType::UInt32 });
                        }
                    }
                }
            }
        }

        cmdList.endRendering();
    };
}
