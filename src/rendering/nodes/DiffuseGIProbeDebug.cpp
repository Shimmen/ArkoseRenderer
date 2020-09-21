#include "DiffuseGIProbeDebug.h"

#include "CameraState.h"
#include "utility/Logging.h"
#include <imgui.h>

std::string DiffuseGIProbeDebug::name()
{
    return "diffuse-gi-probe-debug";
}

DiffuseGIProbeDebug::DiffuseGIProbeDebug(Scene& scene, DiffuseGINode::ProbeGridDescription gridDescription)
    : RenderGraphNode(DiffuseGIProbeDebug::name())
    , m_scene(scene)
    , m_grid(gridDescription)
{
}

void DiffuseGIProbeDebug::constructNode(Registry& reg)
{
    constexpr int rings = 16;
    constexpr int sectors = 16;

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

RenderGraphNode::ExecuteCallback DiffuseGIProbeDebug::constructFrame(Registry& reg) const
{
    BindingSet& cameraBindingSet = *reg.getBindingSet("scene", "cameraSet");
    Texture& depthTexture = *reg.getTexture("g-buffer", "depth").value();
    Texture& colorTexture = *reg.getTexture("forward", "color").value();

    // TODO: Don't clear the imported textures!!
    //RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture, LoadOp::Load },
    //                                                      { RenderTarget::AttachmentType::Depth, &depthTexture, LoadOp::Clear } });
    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, &colorTexture, LoadOp::Clear },
                                                          { RenderTarget::AttachmentType::Depth, &depthTexture, LoadOp::Clear } });

    

    Shader debugShader = Shader::createBasicRasterize("diffuse-gi/probe-debug.vert", "diffuse-gi/probe-debug.frag");
    RenderStateBuilder stateBuilder { renderTarget, debugShader, VertexLayout::positionOnly() };
    stateBuilder.addBindingSet(cameraBindingSet);
    stateBuilder.writeDepth = true;
    stateBuilder.testDepth = true;
    RenderState& renderState = reg.createRenderState(stateBuilder);

    return [&](const AppState& appState, CommandList& cmdList) {
        static float probeScale = 0.1f;
        ImGui::SliderFloat("Probe size (m)", &probeScale, 0.01f, 1.0f);

        cmdList.beginRendering(renderState, ClearColor(1.0f, 0.0f, 1.0f), 1.0f);
        cmdList.bindSet(cameraBindingSet, 0);
        cmdList.pushConstant(ShaderStageVertex, probeScale, sizeof(vec4));
        {
            for (int z = 0; z < m_grid.gridDimensions.depth(); ++z) {
                for (int y = 0; y < m_grid.gridDimensions.height(); ++y) {
                    for (int x = 0; x < m_grid.gridDimensions.width(); ++x) {

                        vec4 probeLocation = vec4(m_grid.offsetToFirst + vec3(x, y, z) * m_grid.probeSpacing, 0.0);
                        cmdList.pushConstant(ShaderStageVertex, probeLocation, 0);

                        cmdList.drawIndexed(*m_sphereVertexBuffer, *m_sphereIndexBuffer, m_indexCount, IndexType::UInt16);
                    }
                }
            }
        }
        cmdList.endRendering();
    };
}
