#include "HairShadowNode.h"

#include "core/math/Frustum.h"
#include "rendering/GpuScene.h"
#include "scene/lights/Light.h"
#include "rendering/util/ScopedDebugZone.h"
#include "utility/Profiling.h"
#include <fmt/format.h>
#include <imgui.h>
#include <core/Types.h>

void HairShadowNode::drawGui()
{
    ImGui::Text("Hair depth map");
    drawTextureVisualizeGui(*m_depthMap);
}

RenderPipelineNode::ExecuteCallback HairShadowNode::construct(GpuScene& scene, Registry& reg)
{
    m_depthMap = &reg.createTexture2D({ 4096, 4096 },
                                      Texture::Format::Depth32F,
                                      Texture::Filters::linear(),
                                      Texture::Mipmap::None,
                                      ImageWrapModes::clampAllToEdge());

    RenderTarget& depthRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, m_depthMap, LoadOp::Clear, StoreOp::Store } });

    Shader depthOnlyShader = Shader::createVertexOnly("hair/shadowMap.vert");
    RenderStateBuilder depthRenderStateBuilder { depthRenderTarget, depthOnlyShader, { VertexComponent::Position3F } };
    depthRenderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneObjectSet"));
    depthRenderStateBuilder.primitiveType = PrimitiveType::LineStrip;
    depthRenderStateBuilder.enablePrimitiveRestart = true;
    RenderState& depthRenderState = reg.createRenderState(depthRenderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {
        // TODO: Support multiple lights
        if (DirectionalLight* mainDirectionalLight = scene.scene().firstDirectionalLight()) {
            // TODO: Make tight bounds around the hair
            mat4 projectionFromWorld = mainDirectionalLight->viewProjection();

            cmdList.beginRendering(depthRenderState, ClearValue::blackAtMaxDepth());
            cmdList.setNamedUniform("projectionFromWorld", projectionFromWorld);

            cmdList.bindVertexBuffer(scene.vertexManager().hairPositionVertexBuffer(), scene.vertexManager().hairPositionVertexLayout().packedVertexSize(), 0);
            cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

            for (std::unique_ptr<HairInstance>& hairInstance : scene.hairInstances()) {

                if (!hairInstance->drawableHandle()) {
                    continue;
                }

                HairMesh* hairMesh = scene.hairMeshForHandle(hairInstance->hair());
                if (hairMesh == nullptr || !hairMesh->valid()) {
                    continue;
                }

                std::string zoneName = fmt::format("HairMesh [{}]", hairInstance->name);
                ScopedDebugZone zone { cmdList, zoneName };

                DrawCallDescription drawCallDesc = hairMesh->drawCallDescription();
                drawCallDesc.firstInstance = hairInstance->drawableHandle().indexOfType<u32>();
                cmdList.issueDrawCall(drawCallDesc);
            }

            cmdList.endRendering();
        }
    };
}
