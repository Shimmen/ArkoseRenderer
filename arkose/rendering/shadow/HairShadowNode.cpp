#include "HairShadowNode.h"

#include "core/math/Frustum.h"
#include "rendering/GpuScene.h"
#include "scene/lights/Light.h"
#include "rendering/util/ScopedDebugZone.h"
#include <fmt/format.h>
#include <imgui.h>
#include <core/Types.h>

void HairShadowNode::drawGui()
{
    ImGui::Text("Depth map (main view)");
    drawTextureVisualizeGui(*m_depthMap);

    ImGui::Text("Shadow map");
    drawTextureVisualizeGui(*m_shadowMap);

    ImGui::Text("Deep opacity map");
    drawTextureVisualizeGui(*m_deepOpacityMap);
}

template<typename Callback>
void forEachHairInstance(GpuScene const& scene, CommandList& cmdList, Callback&& callback)
{
    for (std::unique_ptr<HairInstance> const& hairInstance : scene.hairInstances()) {
        if (!hairInstance->drawableHandle()) {
            continue;
        }
        HairMesh const* hairMesh = scene.hairMeshForHandle(hairInstance->hair());
        if (hairMesh == nullptr || !hairMesh->valid() || !scene.vertexManager().isHairMeshLoaded(*hairMesh)) {
            continue;
        }

        std::string zoneName = fmt::format("HairMesh [{}]", hairInstance->name);
        ScopedDebugZone hairZone { cmdList, zoneName };

        callback(*hairInstance);
    }
}

void drawHairInstanceAsLineStrip(GpuScene& scene, CommandList& cmdList, HairInstance& hairInstance)
{
    HairMesh* hairMesh = scene.hairMeshForHandle(hairInstance.hair());
    DrawCallDescription drawCallDesc = hairMesh->drawCallDescriptionAsLineStrip();
    drawCallDesc.firstInstance = hairInstance.drawableHandle().indexOfType<u32>();
    cmdList.issueDrawCall(drawCallDesc);
}

RenderPipelineNode::ExecuteCallback HairShadowNode::construct(GpuScene& scene, Registry& reg)
{
    m_depthMap = &reg.createTexture2D({ 2048, 2048 },
                                      Texture::Format::Depth32F,
                                      Texture::Filters::nearest(),
                                      Texture::Mipmap::None,
                                      ImageWrapModes::clampAllToEdge());

    m_shadowMap = &reg.createTexture2D({ 4096, 4096 },
                                       Texture::Format::Depth32F,
                                       Texture::Filters::nearest(),
                                       Texture::Mipmap::None,
                                       ImageWrapModes::clampAllToEdge());

    m_deepOpacityMap = &reg.createTexture2D({ 4096, 4096 },
                                            Texture::Format::RGBA8,
                                            Texture::Filters::nearest(),
                                            Texture::Mipmap::None,
                                            ImageWrapModes::clampAllToEdge());

    reg.publish("HairDepthMap", *m_depthMap);
    reg.publish("HairShadowMap", *m_shadowMap);
    reg.publish("HairDeepOpacityMap", *m_deepOpacityMap);

    Shader depthOnlyShader = Shader::createVertexOnly("hair/shadowMap.vert");

    RenderTarget& depthRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, m_depthMap, LoadOp::Clear, StoreOp::Store } });
    RenderStateBuilder depthRenderStateBuilder { depthRenderTarget, depthOnlyShader, { VertexComponent::Position3F } };
    depthRenderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneGeometrySet"));
    depthRenderStateBuilder.primitiveType = PrimitiveType::LineStrip;
    depthRenderStateBuilder.enablePrimitiveRestart = true;
    RenderState& depthRenderState = reg.createRenderState(depthRenderStateBuilder);

    RenderTarget& shadowRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, m_shadowMap, LoadOp::Clear, StoreOp::Store } });
    RenderStateBuilder shadowRenderStateBuilder { shadowRenderTarget, depthOnlyShader, { VertexComponent::Position3F } };
    shadowRenderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneGeometrySet"));
    shadowRenderStateBuilder.primitiveType = PrimitiveType::LineStrip;
    shadowRenderStateBuilder.enablePrimitiveRestart = true;
    RenderState& shadowRenderState = reg.createRenderState(shadowRenderStateBuilder);

    BindingSet& hairShadowMapBindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*m_shadowMap) });

    RenderTarget& deepOpacityRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, m_deepOpacityMap, LoadOp::Clear, StoreOp::Store, RenderTargetBlendMode::AdditiveIncludingAlpha } });
    Shader deepOpacityShader = Shader::createBasicRasterize("hair/deepOpacityMap.vert", "hair/deepOpacityMap.frag");
    RenderStateBuilder deepOpacityRenderStateBuilder { deepOpacityRenderTarget, deepOpacityShader, { VertexComponent::Position3F } };
    deepOpacityRenderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneGeometrySet"));
    deepOpacityRenderStateBuilder.stateBindings().at(1, hairShadowMapBindingSet);
    deepOpacityRenderStateBuilder.primitiveType = PrimitiveType::LineStrip;
    deepOpacityRenderStateBuilder.enablePrimitiveRestart = true;
    RenderState& deepOpacityRenderState = reg.createRenderState(deepOpacityRenderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        cmdList.bindVertexBuffer(scene.vertexManager().hairPositionVertexBuffer(), scene.vertexManager().hairPositionVertexLayout().packedVertexSize(), 0);
        cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

        {
            ScopedDebugZone zone { cmdList, "Depth map" };

            mat4 mainCameraProjectionFromWorld = scene.camera().projectionMatrix() * scene.camera().viewMatrix();

            cmdList.beginRendering(depthRenderState, ClearValue::blackAtMaxDepth());
            cmdList.setNamedUniform("projectionFromWorld", mainCameraProjectionFromWorld);

            forEachHairInstance(scene, cmdList, [&](HairInstance& hairInstance) {
                drawHairInstanceAsLineStrip(scene, cmdList, hairInstance);
            });

            cmdList.endRendering();
        }

        if (DirectionalLight* mainDirectionalLight = scene.scene().firstDirectionalLight()) {
            mat4 projectionFromWorld = mainDirectionalLight->viewProjection();

            {
                ScopedDebugZone zone { cmdList, "Shadow map" };

                cmdList.beginRendering(shadowRenderState, ClearValue::blackAtMaxDepth());
                cmdList.setNamedUniform("projectionFromWorld", projectionFromWorld);

                forEachHairInstance(scene, cmdList, [&](HairInstance& hairInstance) {
                    drawHairInstanceAsLineStrip(scene, cmdList, hairInstance);
                });

                cmdList.endRendering();
            }

            {
                ScopedDebugZone zone { cmdList, "Deep opacity map" };

                cmdList.beginRendering(deepOpacityRenderState, ClearValue::blackAtMaxDepth());
                cmdList.setNamedUniform("projectionFromWorld", projectionFromWorld);
                cmdList.setNamedUniform("shadowMapDepthRange", mainDirectionalLight->shadowMapWorldExtent);

                forEachHairInstance(scene, cmdList, [&](HairInstance& hairInstance) {
                    drawHairInstanceAsLineStrip(scene, cmdList, hairInstance);
                });

                cmdList.endRendering();
            }
        }
    };
}
