#include "PickingNode.h"

#include "CameraState.h"
#include "LightData.h"
#include "utility/Logging.h"
#include <imgui.h>
#include <mooslib/vector.h>

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
    Buffer& transformDataBuffer = reg.createBuffer(PICKING_MAX_DRAWABLES * sizeof(mat4), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::TransferOptimal);

    Texture& indexMap = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R32);
    Texture& indexDepthMap = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::Depth32F);
    RenderTarget& indexMapRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &indexMap, LoadOp::Clear, StoreOp::Store },
                                                                  { RenderTarget::AttachmentType::Depth, &indexDepthMap, LoadOp::Clear, StoreOp::Ignore } });

    Shader drawIndexShader = Shader::createBasicRasterize("picking/drawIndices.vert", "picking/drawIndices.frag");
    VertexLayout vertexLayout = { sizeof(vec3), { { 0, VertexAttributeType::Float3, 0 } } };
    BindingSet& drawIndexBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, reg.getBuffer("scene", "camera") },
                                                             { 1, ShaderStageVertex, &transformDataBuffer } });
    RenderStateBuilder renderStateBuilder(indexMapRenderTarget, drawIndexShader, vertexLayout);
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
            mat4 objectTransforms[PICKING_MAX_DRAWABLES];
            numMeshes = m_scene.forEachMesh([&](size_t index, Mesh& mesh) {
                objectTransforms[index] = mesh.transform().worldMatrix();
                mesh.ensureVertexBuffer({ VertexComponent::Position3F });
                mesh.ensureIndexBuffer();
            });
            transformDataBuffer.updateData(objectTransforms, numMeshes * sizeof(mat4));

            cmdList.beginRendering(drawIndicesState, ClearColor(1, 0, 1), 1.0f);
            cmdList.bindSet(drawIndexBindingSet, 0);

            m_scene.forEachMesh([&](size_t index, Mesh& mesh) {
                cmdList.drawIndexed(mesh.vertexBuffer({ VertexComponent::Position3F }), mesh.indexBuffer(), mesh.indexCount(), mesh.indexType(), static_cast<uint32_t>(index));
            });

            cmdList.endRendering();
        }

        {
            cmdList.setComputeState(collectState);
            cmdList.bindSet(collectIndexBindingSet, 0);

            vec2 pickLocation = Input::instance().mousePosition();
            cmdList.pushConstant(ShaderStageCompute, pickLocation, 0);

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
