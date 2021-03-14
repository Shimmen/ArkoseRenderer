#include "PickingNode.h"

#include "CameraState.h"
#include "LightData.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>
#include <moos/vector.h>

// Shared with shaders
#include "Picking.h"

std::string PickingNode::name()
{
    return "picking";
}

PickingNode::PickingNode(Scene& scene)
    : RenderGraphNode(PickingNode::name())
    , m_scene(scene)
{
}

RenderGraphNode::ExecuteCallback PickingNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    Buffer& transformDataBuffer = reg.createBuffer(PICKING_MAX_DRAWABLES * sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

    Texture& indexMap = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R32);
    Texture& indexDepthMap = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::Depth32F);
    RenderTarget& indexMapRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &indexMap, LoadOp::Clear, StoreOp::Store },
                                                                  { RenderTarget::AttachmentType::Depth, &indexDepthMap, LoadOp::Clear, StoreOp::Discard } });

    Shader drawIndexShader = Shader::createBasicRasterize("picking/drawIndices.vert", "picking/drawIndices.frag");
    BindingSet& drawIndexBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, reg.getBuffer("scene", "camera") },
                                                             { 1, ShaderStageVertex, &transformDataBuffer } });
    RenderStateBuilder renderStateBuilder(indexMapRenderTarget, drawIndexShader, VertexLayout { VertexComponent::Position3F });
    renderStateBuilder.addBindingSet(drawIndexBindingSet);
    RenderState& drawIndicesState = reg.createRenderState(renderStateBuilder);

    Buffer& pickedIndexBuffer = reg.createBuffer(sizeof(moos::u32), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::Readback);
    Shader collectorShader = Shader::createCompute("picking/collectIndex.comp");
    BindingSet& collectIndexBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &indexMap, ShaderBindingType::StorageImage },
                                                                { 1, ShaderStageCompute, &pickedIndexBuffer } });
    ComputeState& collectState = reg.createComputeState(collectorShader, { &collectIndexBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {
        int numMeshes;
        {
            {
                SCOPED_PROFILE_ZONE_NAMED("Updating transform data");
                mat4 objectTransforms[PICKING_MAX_DRAWABLES];
                numMeshes = m_scene.forEachMesh([&](size_t index, Mesh& mesh) {
                    objectTransforms[index] = mesh.transform().worldMatrix();
                    mesh.ensureDrawCall({ VertexComponent::Position3F }, m_scene);
                });
                transformDataBuffer.updateData(objectTransforms, numMeshes * sizeof(mat4));
            }

            cmdList.beginRendering(drawIndicesState, ClearColor(1, 0, 1), 1.0f);
            cmdList.bindSet(drawIndexBindingSet, 0);

            cmdList.bindVertexBuffer(m_scene.globalVertexBufferForLayout({ VertexComponent::Position3F }));
            cmdList.bindIndexBuffer(m_scene.globalIndexBuffer(), m_scene.globalIndexBufferType());

            {
                SCOPED_PROFILE_ZONE_NAMED("Issuing draw calls");
                m_scene.forEachMesh([&](size_t index, Mesh& mesh) {

                    DrawCall drawCall = mesh.getDrawCall({ VertexComponent::Position3F }, m_scene);
                    drawCall.firstInstance = static_cast<uint32_t>(index); // TODO: Put this in some buffer instead!

                    cmdList.issueDrawCall(drawCall);
                });
            }

            cmdList.endRendering();
        }

        {
            SCOPED_PROFILE_ZONE_NAMED("Picking");

            cmdList.setComputeState(collectState);
            cmdList.bindSet(collectIndexBindingSet, 0);

            vec2 pickLocation = Input::instance().mousePosition();
            cmdList.setNamedUniform("mousePosition", pickLocation);

            cmdList.dispatch(indexMap.extent(), { 16, 16, 1 });
        }

        auto didClick = [this](Button button) -> bool {
            auto& input = Input::instance();

            if (input.wasButtonPressed(button))
                m_mouseDownLocation = input.mousePosition();

            if (input.wasButtonReleased(button) && m_mouseDownLocation.has_value()) {
                float distance = moos::distance(input.mousePosition(), m_mouseDownLocation.value());
                m_mouseDownLocation.reset();
                if (distance < 4.0f)
                    return true;
            }

            return false;
        };

        if (!didClick(Button::Middle))
            return;

        moos::u32 selectedIndex;
        cmdList.slowBlockingReadFromBuffer(pickedIndexBuffer, 0, sizeof(moos::u32), &selectedIndex);
        if (selectedIndex < numMeshes) {
            SCOPED_PROFILE_ZONE_NAMED("Finding mesh");
            m_scene.forEachMesh([&](size_t index, Mesh& mesh) {
                if (index == selectedIndex) {
                    //LogInfo("Selected mesh (global index %u) of model '%s'\n", selectedIndex, mesh.model()->name().c_str());
                    m_scene.setSelectedMesh(&mesh);
                    m_scene.setSelectedModel(mesh.model());
                }
            });
        } else {
            //LogInfo("Unselecting mesh & model\n");
            m_scene.setSelectedMesh(nullptr);
            m_scene.setSelectedModel(nullptr);
        }
    };
}
