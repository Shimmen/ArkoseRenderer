#include "DiffuseGIProbeDebug.h"

#include "CameraState.h"
#include "ProbeDebug.h"
#include "utility/Logging.h"
#include <imgui.h>

std::string DiffuseGIProbeDebug::name()
{
    return "diffuse-gi-probe-debug";
}

DiffuseGIProbeDebug::DiffuseGIProbeDebug(Scene& scene)
    : RenderGraphNode(DiffuseGIProbeDebug::name())
    , m_scene(scene)
{
}

void DiffuseGIProbeDebug::constructNode(Registry& reg)
{
#if PROBE_DEBUG_VIZ == PROBE_DEBUG_VISUALIZE_COLOR
    m_probeData = reg.getTexture("diffuse-gi", "irradianceProbes").value();
#elif (PROBE_DEBUG_VIZ == PROBE_DEBUG_VISUALIZE_DISTANCE) || (PROBE_DEBUG_VIZ == PROBE_DEBUG_VISUALIZE_DISTANCE2)
    m_probeData = reg.getTexture("diffuse-gi", "filteredDistanceProbes").value();
#endif

    setUpSphereRenderData(reg);
}

RenderGraphNode::ExecuteCallback DiffuseGIProbeDebug::constructFrame(Registry& reg) const
{
    Texture& depthTexture = *reg.getTexture("g-buffer", "depth").value();
    Texture& colorTexture = *reg.getTexture("forward", "color").value();
    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture, LoadOp::Load, StoreOp::Store },
                                                          { RenderTarget::AttachmentType::Depth, &depthTexture, LoadOp::Load, StoreOp::Discard } });

    BindingSet& probeDataBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, m_probeData, ShaderBindingType::TextureSampler } });

    Shader debugShader = Shader::createBasicRasterize("diffuse-gi/probe-debug.vert", "diffuse-gi/probe-debug.frag");
    RenderStateBuilder stateBuilder { renderTarget, debugShader, VertexLayout::positionOnly() };
    BindingSet& cameraBindingSet = *reg.getBindingSet("scene", "cameraSet");
    stateBuilder.addBindingSet(cameraBindingSet).addBindingSet(probeDataBindingSet);
    stateBuilder.writeDepth = true;
    stateBuilder.testDepth = true;
    RenderState& renderState = reg.createRenderState(stateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        static float probeScale = 0.1f;
        ImGui::SliderFloat("Probe size (m)", &probeScale, 0.01f, 1.0f);

        cmdList.beginRendering(renderState);
        cmdList.bindSet(cameraBindingSet, 0);
        cmdList.bindSet(probeDataBindingSet, 1);
        cmdList.pushConstant(ShaderStageVertex, probeScale, 0);
        {
            for (size_t probeIdx = 0; probeIdx < m_scene.probeGrid().probeCount(); ++probeIdx) {
                auto probeIdx3D = m_scene.probeGrid().probeIndexFromLinear(probeIdx);
                vec4 probeLocation = vec4(m_scene.probeGrid().probePositionForIndex(probeIdx3D), 0.0f);

                cmdList.pushConstant(ShaderStageVertex, probeLocation, 1 * sizeof(vec4));
                cmdList.pushConstant(ShaderStageVertex, (int)probeIdx, 2 * sizeof(vec4));

                cmdList.drawIndexed(*m_sphereVertexBuffer, *m_sphereIndexBuffer, m_indexCount, IndexType::UInt16);
            }
        }
        cmdList.endRendering();
    };
}

void DiffuseGIProbeDebug::setUpSphereRenderData(Registry& reg)
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

        m_indexCount = indices.size();
    }

    m_sphereVertexBuffer = &reg.createBuffer(std::move(positions), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);
    m_sphereIndexBuffer = &reg.createBuffer(std::move(indices), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
}
