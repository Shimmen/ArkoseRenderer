#include "PickingNode.h"

#include "CameraState.h"
#include "LightData.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>
#include <moos/vector.h>

PickingNode::PickingNode(Scene& scene)
    : m_scene(scene)
{
}

RenderPipelineNode::ExecuteCallback PickingNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    Texture& indexMap = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R32);
    Texture& indexDepthMap = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::Depth32F);
    RenderTarget& indexMapRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &indexMap, LoadOp::Clear, StoreOp::Store },
                                                                  { RenderTarget::AttachmentType::Depth, &indexDepthMap, LoadOp::Clear, StoreOp::Discard } });

    Buffer& transformDataBuffer = reg.createBuffer(256 * sizeof(mat4), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::TransferOptimal);
    BindingSet& drawIndexBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &transformDataBuffer } });

    Shader drawIndexShader = Shader::createBasicRasterize("picking/drawIndices.vert", "picking/drawIndices.frag");
    RenderStateBuilder renderStateBuilder(indexMapRenderTarget, drawIndexShader, VertexLayout { VertexComponent::Position3F });
    renderStateBuilder.stateBindings().at(0, drawIndexBindingSet);
    RenderState& drawIndicesState = reg.createRenderState(renderStateBuilder);

    Buffer& pickedIndexBuffer = reg.createBuffer(sizeof(moos::u32), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::Readback);
    Shader collectorShader = Shader::createCompute("picking/collectIndex.comp");
    BindingSet& collectIndexBindingSet = reg.createBindingSet({ { 0, ShaderStageCompute, &indexMap, ShaderBindingType::StorageImage },
                                                                { 1, ShaderStageCompute, &pickedIndexBuffer } });
    ComputeState& collectState = reg.createComputeState(collectorShader, { &collectIndexBindingSet });

    return [&](const AppState& appState, CommandList& cmdList) {

        // TODO: Implement some proper CPU readback context so we know for sure that the previous result
        // is ready at this point. Just because it's from the previous frame doesn't mean it must be done.
        // What if we submit the queue and immediately start work on the next frame before the first is
        // even started? And many more similar scenarios.
        if (m_lastResultBuffer.has_value()) {

            moos::u32 selectedIndex;
            cmdList.slowBlockingReadFromBuffer(*m_lastResultBuffer.value(), 0, sizeof(moos::u32), &selectedIndex);
            if (selectedIndex < m_scene.meshCount()) {
                m_scene.forEachMesh([&](size_t index, Mesh& mesh) {
                    if (index == selectedIndex) {
                        m_scene.setSelectedMesh(&mesh);
                        m_scene.setSelectedModel(mesh.model());
                    }
                });
            } else {
                m_scene.setSelectedMesh(nullptr);
                m_scene.setSelectedModel(nullptr);
            }

            m_lastResultBuffer.reset();
        }

        if (didClick(Button::Middle)) {

            std::vector<mat4> objectTransforms {}; 
            m_scene.forEachMesh([&](size_t index, Mesh& mesh) {
                objectTransforms.push_back(mesh.transform().worldMatrix());
                mesh.ensureDrawCallIsAvailable({ VertexComponent::Position3F }, m_scene);
            });
            transformDataBuffer.updateDataAndGrowIfRequired(objectTransforms.data(), objectTransforms.size() * sizeof(mat4));

            cmdList.beginRendering(drawIndicesState, ClearColor::srgbColor(1, 0, 1), 1.0f);
            cmdList.setNamedUniform("projectionFromWorld", m_scene.camera().viewProjectionMatrix());

            cmdList.bindVertexBuffer(m_scene.globalVertexBufferForLayout({ VertexComponent::Position3F }));
            cmdList.bindIndexBuffer(m_scene.globalIndexBuffer(), m_scene.globalIndexBufferType());

            m_scene.forEachMesh([&](size_t index, Mesh& mesh) {
                DrawCallDescription drawCall = mesh.drawCallDescription({ VertexComponent::Position3F }, m_scene);
                drawCall.firstInstance = static_cast<uint32_t>(index);
                cmdList.issueDrawCall(drawCall);
            });

            cmdList.endRendering();

            cmdList.setComputeState(collectState);
            cmdList.bindSet(collectIndexBindingSet, 0);

            vec2 pickLocation = Input::instance().mousePosition();
            cmdList.setNamedUniform("mousePosition", pickLocation);

            cmdList.dispatch(indexMap.extent(), { 16, 16, 1 });
            m_lastResultBuffer = &pickedIndexBuffer;
        }
    };
}

bool PickingNode::didClick(Button button) const
{
    static std::optional<vec2> mouseDownLocation {};
    auto& input = Input::instance();

    if (input.wasButtonPressed(button))
        mouseDownLocation = input.mousePosition();

    if (input.wasButtonReleased(button) && mouseDownLocation.has_value()) {
        float distance = moos::distance(input.mousePosition(), mouseDownLocation.value());
        mouseDownLocation.reset();
        if (distance < 4.0f)
            return true;
    }

    return false;
}
