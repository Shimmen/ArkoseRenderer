#include "DDGIProbeDebug.h"

#include "rendering/scene/GpuScene.h"
#include "core/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>

// Shared shader headers
#include "DDGIData.h"

RenderPipelineNode::ExecuteCallback DDGIProbeDebug::construct(GpuScene& scene, Registry& reg)
{
    if (!reg.hasPreviousNode("DDGI"))
        return RenderPipelineNode::NullExecuteCallback;

    ///////////////////////
    // constructNode
    BindingSet& m_ddgiSamplingSet = *reg.getBindingSet("DDGISamplingSet");
    const_cast<DDGIProbeDebug*>(this)->setUpSphereRenderData(scene, reg);
    ///////////////////////

    Texture& depthTexture = *reg.getTexture("SceneDepth");
    Texture& colorTexture = *reg.getTexture("SceneColor");
    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture, LoadOp::Load, StoreOp::Store },
                                                          { RenderTarget::AttachmentType::Depth, &depthTexture, LoadOp::Load, StoreOp::Discard } });

    Shader debugShader = Shader::createBasicRasterize("ddgi/probeDebug.vert", "ddgi/probeDebug.frag");
    RenderStateBuilder stateBuilder { renderTarget, debugShader, VertexLayout { VertexComponent::Position3F }};
    stateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneCameraSet"));
    stateBuilder.stateBindings().at(1, m_ddgiSamplingSet);
    stateBuilder.writeDepth = true;
    stateBuilder.testDepth = true;
    RenderState& renderState = reg.createRenderState(stateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        SCOPED_PROFILE_ZONE();

        ImGui::Text("Debug visualisation:");
        static int debugVisualisation = DDGI_PROBE_DEBUG_VISUALIZE_DISABLED;
        if (ImGui::RadioButton("Disabled", debugVisualisation == DDGI_PROBE_DEBUG_VISUALIZE_DISABLED))
            debugVisualisation = DDGI_PROBE_DEBUG_VISUALIZE_DISABLED;
        if (ImGui::RadioButton("Irradiance", debugVisualisation == DDGI_PROBE_DEBUG_VISUALIZE_IRRADIANCE))
            debugVisualisation = DDGI_PROBE_DEBUG_VISUALIZE_IRRADIANCE;
        if (ImGui::RadioButton("Visibility distance", debugVisualisation == DDGI_PROBE_DEBUG_VISUALIZE_DISTANCE))
            debugVisualisation = DDGI_PROBE_DEBUG_VISUALIZE_DISTANCE;
        if (ImGui::RadioButton("Visibility distance^2", debugVisualisation == DDGI_PROBE_DEBUG_VISUALIZE_DISTANCE2))
            debugVisualisation = DDGI_PROBE_DEBUG_VISUALIZE_DISTANCE2;

        if (debugVisualisation == DDGI_PROBE_DEBUG_VISUALIZE_DISABLED)
            return;

        static float probeScale = 0.05f;
        ImGui::SliderFloat("Probe size (m)", &probeScale, 0.01f, 1.0f);

        static float distanceScale = 0.002f;
        ImGui::SliderFloat("Distance scale", &distanceScale, 0.001f, 0.1f);

        cmdList.beginRendering(renderState);
        cmdList.setNamedUniform("probeScale", probeScale);
        cmdList.setNamedUniform("distanceScale", distanceScale);
        cmdList.setNamedUniform("debugVisualisation", debugVisualisation);

        // TODO: Use instanced rendering instead.. it's sufficient for debug visualisation but it's not great.
        for (int probeIdx = 0; probeIdx < scene.scene().probeGrid().probeCount(); ++probeIdx) {
            
            ivec3 probeIdx3D = scene.scene().probeGrid().probeIndexFromLinear(probeIdx);
            vec3 probeLocation = scene.scene().probeGrid().probePositionForIndex(probeIdx3D);

            cmdList.setNamedUniform("probeGridCoord", probeIdx3D);
            cmdList.setNamedUniform("probeLocation", probeLocation);

            cmdList.drawIndexed(*m_sphereVertexBuffer, *m_sphereIndexBuffer, m_indexCount, IndexType::UInt16);
        }

        cmdList.endRendering();
    };
}

void DDGIProbeDebug::setUpSphereRenderData(GpuScene& scene, Registry& reg)
{
    constexpr int rings = 48;
    constexpr int sectors = 48;

    using namespace moos;

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

        m_indexCount = (uint32_t)indices.size();
    }

    m_sphereVertexBuffer = &reg.createBuffer(std::move(positions), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
    m_sphereIndexBuffer = &reg.createBuffer(std::move(indices), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
}