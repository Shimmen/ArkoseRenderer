#include "TranslucencyNode.h"

#include "core/Types.h"
#include "rendering/GpuScene.h"
#include "rendering/util/ScopedDebugZone.h"
#include "utility/Profiling.h"
#include <imgui.h>

// Shared shader headers
#include "shaders/shared/IndirectData.h"
#include "shaders/shared/LightData.h"

RenderPipelineNode::ExecuteCallback TranslucencyNode::construct(GpuScene& scene, Registry& reg)
{
    Texture* colorTexture = reg.getTexture("SceneColor");
    Texture* depthTexture = reg.getTexture("SceneDepth");

    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, colorTexture, LoadOp::Load, StoreOp::Store, RenderTargetBlendMode::Additive },
                                                          { RenderTarget::AttachmentType::Depth, depthTexture, LoadOp::Load, StoreOp::Store } });

    RenderState& renderState = makeRenderState(reg, scene, renderTarget);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        std::vector<TranslucentMeshSegmentInstance> translucentInstances = generateSortedDrawList(scene);

        for (auto const& instance : translucentInstances) {
            instance.meshSegment->ensureDrawCallIsAvailable(m_vertexLayout, scene);
        }

        cmdList.bindVertexBuffer(scene.globalVertexBufferForLayout(m_vertexLayout));
        cmdList.bindIndexBuffer(scene.globalIndexBuffer(), scene.globalIndexBufferType());


        cmdList.beginRendering(renderState);
        cmdList.setNamedUniform("ambientAmount", scene.preExposedAmbient());
        cmdList.setNamedUniform("frustumJitterCorrection", scene.camera().frustumJitterUVCorrection());
        cmdList.setNamedUniform("invTargetSize", renderTarget.extent().inverse());

        for (TranslucentMeshSegmentInstance const& instance : translucentInstances) {
            DrawCallDescription drawCall = instance.meshSegment->drawCallDescription(m_vertexLayout, scene);
            drawCall.firstInstance = instance.drawableIdx;
            cmdList.issueDrawCall(drawCall);
        }

        cmdList.endRendering();
    };
}

RenderState& TranslucencyNode::makeRenderState(Registry& reg, GpuScene const& scene, RenderTarget& renderTarget) const
{
    // TODO: Specify Translucent BRDF (right now we're just using a basic microfacet
    // BRDF for translucency which isn't entirely correct..

    std::vector<ShaderDefine> shaderDefines {};
    shaderDefines.push_back(ShaderDefine::makeInt("FORWARD_BLEND_MODE", BLEND_MODE_TRANSLUCENT));

    Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag", shaderDefines);

    RenderStateBuilder renderStateBuilder { renderTarget, shader, m_vertexLayout };
    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.writeDepth = false;
    renderStateBuilder.cullBackfaces = true;

    Texture* dirLightProjectedShadow = reg.getTexture("DirectionalLightProjectedShadow");
    Texture* sphereLightProjectedShadow = reg.getTexture("SphereLightProjectedShadow");
    Texture* localLightShadowMapAtlas = reg.getTexture("LocalLightShadowMapAtlas");
    Buffer* localLightShadowAllocations = reg.getBuffer("LocalLightShadowAllocations");

    // Allow running without shadows
    if (!dirLightProjectedShadow || !sphereLightProjectedShadow || !localLightShadowMapAtlas || !localLightShadowAllocations) {
        Texture& placeholderTex = reg.createPixelTexture(vec4(1.0f), false);
        Buffer& placeholderBuffer = reg.createBufferForData(std::vector<int>(0), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOptimal);
        dirLightProjectedShadow = dirLightProjectedShadow ? dirLightProjectedShadow : &placeholderTex;
        sphereLightProjectedShadow = sphereLightProjectedShadow ? sphereLightProjectedShadow : &placeholderTex;
        localLightShadowMapAtlas = localLightShadowMapAtlas ? localLightShadowMapAtlas : &placeholderTex;
        localLightShadowAllocations = localLightShadowAllocations ? localLightShadowAllocations : &placeholderBuffer;
    }

    BindingSet& shadowBindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*dirLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*sphereLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*localLightShadowMapAtlas),
                                                          ShaderBinding::storageBuffer(*localLightShadowAllocations) });

    StateBindings& bindings = renderStateBuilder.stateBindings();
    bindings.at(0, *reg.getBindingSet("SceneCameraSet"));
    bindings.at(1, scene.globalMaterialBindingSet());
    bindings.at(2, *reg.getBindingSet("SceneLightSet"));
    bindings.at(3, *reg.getBindingSet("SceneObjectSet"));
    bindings.at(4, shadowBindingSet);

    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName("Translucent");

    return renderState;
}

TranslucencyNode::TranslucentMeshSegmentInstance::TranslucentMeshSegmentInstance(StaticMeshSegment const& inMeshSegment, Transform const& inTransform, u32 inDrawableIdx)
    : meshSegment(&inMeshSegment)
    , transform(&inTransform)
    , drawableIdx(inDrawableIdx)
{
    ARKOSE_ASSERT(meshSegment->blendMode == BlendMode::Translucent);
}

std::vector<TranslucencyNode::TranslucentMeshSegmentInstance> TranslucencyNode::generateSortedDrawList(GpuScene const& scene) const
{
    SCOPED_PROFILE_ZONE();

    std::vector<TranslucentMeshSegmentInstance> instances {};

    vec3 cameraPosition = scene.camera().position();
    geometry::Frustum const& cameraFrustum = scene.camera().frustum();

    // TODO: Consider keeping translucent segments in a separate list so we don't have to iterate all here
    for (auto const& instance : scene.staticMeshInstances()) {
        if (StaticMesh const* staticMesh = scene.staticMeshForInstance(*instance)) {

            constexpr u32 lodIdx = 0;
            StaticMeshLOD const& lod = staticMesh->lodAtIndex(lodIdx);

            if (not staticMesh->hasTranslucentSegments()) {
                continue;
            }

            if (not cameraFrustum.includesSphere(staticMesh->boundingSphere())) {
                continue;
            }

            for (u32 segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
                StaticMeshSegment const& meshSegment = lod.meshSegments[segmentIdx];
                if (meshSegment.blendMode == BlendMode::Translucent) {
                    u32 drawableIdx = instance->drawableHandleForSegmentIndex(segmentIdx).indexOfType<u32>();
                    instances.emplace_back(meshSegment, instance->transform(), drawableIdx);
                }
            }
        }
    }

    // Sort back to front
    std::sort(instances.begin(), instances.end(), [&](TranslucentMeshSegmentInstance const& lhs, TranslucentMeshSegmentInstance const& rhs) {
        float lhsDistance = distance(cameraPosition, lhs.transform->positionInWorld());
        float rhsDistance = distance(cameraPosition, rhs.transform->positionInWorld());
        return lhsDistance > rhsDistance;
    });

    return instances;
}
