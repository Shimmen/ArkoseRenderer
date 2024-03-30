#include "DDGIProbeDebug.h"

#include "rendering/GpuScene.h"
#include "core/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

// Shared shader headers
#include "shaders/shared/DDGIData.h"

void DDGIProbeDebug::drawGui()
{
    ImGui::Text("Debug visualisation:");

    if (ImGui::RadioButton("Disabled", m_debugVisualisation == DDGI_PROBE_DEBUG_VISUALIZE_DISABLED)) {
        m_debugVisualisation = DDGI_PROBE_DEBUG_VISUALIZE_DISABLED;
    }
    if (ImGui::RadioButton("Irradiance", m_debugVisualisation == DDGI_PROBE_DEBUG_VISUALIZE_IRRADIANCE)) {
        m_debugVisualisation = DDGI_PROBE_DEBUG_VISUALIZE_IRRADIANCE;
    }
    if (ImGui::RadioButton("Visibility distance", m_debugVisualisation == DDGI_PROBE_DEBUG_VISUALIZE_DISTANCE)) {
        m_debugVisualisation = DDGI_PROBE_DEBUG_VISUALIZE_DISTANCE;
    }
    if (ImGui::RadioButton("Visibility distance^2", m_debugVisualisation == DDGI_PROBE_DEBUG_VISUALIZE_DISTANCE2)) {
        m_debugVisualisation = DDGI_PROBE_DEBUG_VISUALIZE_DISTANCE2;
    }

    ImGui::SliderFloat("Probe size (m)", &m_probeScale, 0.01f, 1.0f);
    ImGui::SliderFloat("Distance scale", &m_distanceScale, 0.001f, 0.1f);
    ImGui::Checkbox("Render probes with offsets", &m_useProbeOffset);
}

RenderPipelineNode::ExecuteCallback DDGIProbeDebug::construct(GpuScene& scene, Registry& reg)
{
    if (!reg.hasPreviousNode("DDGI"))
        return RenderPipelineNode::NullExecuteCallback;

    m_sphereDrawCall = createSphereRenderData(scene, reg);

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, reg.getTexture("SceneColor"), LoadOp::Load, StoreOp::Store },
                                                          { RenderTarget::AttachmentType::Depth, reg.getTexture("SceneDepth"), LoadOp::Load, StoreOp::Discard } });

    Shader debugShader = Shader::createBasicRasterize("ddgi/probeDebug.vert", "ddgi/probeDebug.frag");
    RenderStateBuilder stateBuilder { renderTarget, debugShader, VertexLayout { VertexComponent::Position3F }};
    stateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneCameraSet"));
    stateBuilder.stateBindings().at(1, *reg.getBindingSet("DDGISamplingSet"));
    stateBuilder.writeDepth = true;
    stateBuilder.testDepth = true;
    RenderState& renderState = reg.createRenderState(stateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (m_debugVisualisation == DDGI_PROBE_DEBUG_VISUALIZE_DISABLED)
            return;

        cmdList.beginRendering(renderState);
        cmdList.setNamedUniform("probeScale", m_probeScale);
        cmdList.setNamedUniform("distanceScale", m_distanceScale);
        cmdList.setNamedUniform("useProbeOffset", m_useProbeOffset);
        cmdList.setNamedUniform("debugVisualisation", m_debugVisualisation);

        DrawCallDescription probesDrawCall = m_sphereDrawCall;
        probesDrawCall.instanceCount = scene.scene().probeGrid().probeCount();

        cmdList.bindVertexBuffer(*probesDrawCall.vertexBuffer, renderState.vertexLayout().packedVertexSize(), 0);
        cmdList.bindIndexBuffer(*probesDrawCall.indexBuffer, probesDrawCall.indexType);
        cmdList.issueDrawCall(probesDrawCall);

        cmdList.endRendering();
    };
}

DrawCallDescription DDGIProbeDebug::createSphereRenderData(GpuScene& scene, Registry& reg)
{
    constexpr int rings = 48;
    constexpr int sectors = 48;

    using namespace ark;

    std::vector<vec3> positions {};
    std::vector<uint16_t> indices {};
    {
        float R = 1.0f / (rings - 1);
        float S = 1.0f / (sectors - 1);

        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < sectors; ++s) {

                float y = std::sin(-(PI / 2.0f) + PI * r * R);
                float x = std::cos(TWO_PI * s * S) * std::sin(PI * r * R);
                float z = std::sin(TWO_PI * s * S) * std::sin(PI * r * R);

                positions.emplace_back(x, y, z);
            }
        }

        for (int r = 0; r < rings - 1; ++r) {
            for (int s = 0; s < sectors - 1; ++s) {

                int i0 = r * sectors + s;
                int i1 = r * sectors + (s + 1);
                int i2 = (r + 1) * sectors + (s + 1);
                int i3 = (r + 1) * sectors + s;

                indices.push_back(i2);
                indices.push_back(i1);
                indices.push_back(i0);

                indices.push_back(i3);
                indices.push_back(i2);
                indices.push_back(i0);
            }
        }
    }

    auto indexCount = static_cast<uint32_t>(indices.size());
    Buffer& vertexBuffer = reg.createBuffer(std::move(positions), Buffer::Usage::Vertex);
    Buffer& indexBuffer = reg.createBuffer(std::move(indices), Buffer::Usage::Index);

    return DrawCallDescription::makeSimpleIndexed(vertexBuffer, indexBuffer, indexCount, IndexType::UInt16);
}
