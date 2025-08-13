#include "ForwardRenderNode.h"

#include "core/Types.h"
#include "rendering/GpuScene.h"
#include "rendering/VertexManager.h"
#include "rendering/util/BlendModeUtil.h"
#include "rendering/util/ScopedDebugZone.h"
#include "scene/MeshInstance.h"
#include "utility/Profiling.h"
#include <imgui.h>

ForwardRenderNode::ForwardRenderNode(Mode mode, ForwardMeshFilter meshFilter, ForwardClearMode clearMode)
    : m_mode(mode)
    , m_meshFilter(meshFilter)
    , m_clearMode(clearMode)
{
}

std::string ForwardRenderNode::name() const
{
    switch (m_mode) {
    case ForwardRenderNode::Mode::Opaque:
        if (m_meshFilter == ForwardMeshFilter::OnlySkeletalMeshes) {
            return "Forward opaque (skeletal meshes)";
        } else {
            return "Forward opaque";
        }
    case ForwardRenderNode::Mode::Translucent:
        return "Translucency";
    }

    ASSERT_NOT_REACHED();
}

RenderPipelineNode::ExecuteCallback ForwardRenderNode::construct(GpuScene& scene, Registry& reg)
{
    m_hasPreviousPrepass = reg.hasPreviousNode("Prepass");

    // Create render target
    RenderTarget& renderTarget = makeRenderTarget(reg, m_mode);

    // Create all render states (PSOs) needed for rendering
    auto& renderStateLookup = reg.allocate<std::unordered_map<u32, RenderState*>>();
    for (DrawKey const& drawKey : DrawKey::createCompletePermutationSet()) {

        // filter out some potential draw states which we don't need

        bool stateForTranslucentMaterials = drawKey.blendMode().value() == BlendMode::Translucent;
        if ((m_mode == Mode::Opaque && stateForTranslucentMaterials)
            || (m_mode == Mode::Translucent && !stateForTranslucentMaterials)) {
            continue;
        }

        // NOTE: Technically explicit velocity doesn't mean it's a skeletal mesh, but in practice it's that way now..
        bool stateForSkeletalMeshes = drawKey.hasExplicityVelocity().value() == true;
        if ((m_meshFilter == ForwardMeshFilter::OnlyStaticMeshes && stateForSkeletalMeshes) ||
            (m_meshFilter == ForwardMeshFilter::OnlySkeletalMeshes && !stateForSkeletalMeshes)) {
            continue;
        }

        renderStateLookup[drawKey.asUint32()] = &makeForwardRenderState(reg, scene, renderTarget, drawKey);
    }

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        cmdList.bindVertexBuffer(scene.vertexManager().positionVertexBuffer(), scene.vertexManager().positionVertexLayout().packedVertexSize(), 0);
        cmdList.bindVertexBuffer(scene.vertexManager().nonPositionVertexBuffer(), scene.vertexManager().nonPositionVertexLayout().packedVertexSize(), 1);
        cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

        if (m_clearMode == ForwardClearMode::ClearBeforeFirstDraw) {
            for (RenderTarget::Attachment const& attachment : renderTarget.colorAttachments()) {
                cmdList.clearTexture(*attachment.texture, ClearValue::blackAtMaxDepth());
            }
            if (!m_hasPreviousPrepass) {
                cmdList.clearTexture(*renderTarget.depthAttachment()->texture, ClearValue::blackAtMaxDepth());
            }
        }

        std::vector<MeshSegmentInstance> instances = generateSortedDrawList(scene, m_mode, m_meshFilter);
        if (instances.empty()) {
            return;
        }

        bool firstDraw = true;

        DrawKey const* currentStateDrawKey = nullptr;
        for (MeshSegmentInstance const& instance : instances) {

            if (currentStateDrawKey == nullptr || instance.drawKey != *currentStateDrawKey) {
                RenderState* renderState = renderStateLookup[instance.drawKey.asUint32()];
                ARKOSE_ASSERT(renderState != nullptr);

                if (!firstDraw) {
                    cmdList.endRendering();
                    cmdList.endDebugLabel();
                }

                cmdList.beginDebugLabel(renderState->name());
                cmdList.beginRendering(*renderState);

                cmdList.setNamedUniform("ambientAmount", scene.preExposedAmbient());
                cmdList.setNamedUniform("frustumJitterCorrection", scene.camera().frustumJitterUVCorrection());
                cmdList.setNamedUniform("invTargetSize", renderTarget.extent().inverse());
                cmdList.setNamedUniform("mipBias", scene.globalMipBias());
                cmdList.setNamedUniform("withMaterialColor", scene.shouldIncludeMaterialColor());

                currentStateDrawKey = &instance.drawKey;
            }

            DrawCallDescription drawCall = DrawCallDescription::fromVertexAllocation(instance.vertexAllocation);
            drawCall.firstInstance = instance.drawableIdx;
            cmdList.issueDrawCall(drawCall);

            firstDraw = false;
        }

        cmdList.endRendering();
        cmdList.endDebugLabel();
    };
}

ForwardRenderNode::MeshSegmentInstance::MeshSegmentInstance(VertexAllocation inVertexAllocation, DrawKey inDrawKey, Transform const& inTransform, u32 inDrawableIdx)
    : vertexAllocation(inVertexAllocation)
    , drawKey(inDrawKey)
    , drawableIdx(inDrawableIdx)
    , transform(&inTransform)
{
}

RenderTarget& ForwardRenderNode::makeRenderTarget(Registry& reg, Mode mode) const
{
    constexpr LoadOp loadOp = LoadOp::Load;
    constexpr StoreOp storeOp = StoreOp::Store;

    Texture* colorTexture = reg.getTexture("SceneColor");
    Texture* depthTexture = reg.getTexture("SceneDepth");

    if (mode == Mode::Translucent) {

        return reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, colorTexture, loadOp, storeOp, RenderTargetBlendMode::AlphaBlending },
                                        { RenderTarget::AttachmentType::Depth, depthTexture, loadOp, storeOp } });

    } else if (mode == Mode::Opaque) {

        Texture* normalVelocityTexture = reg.getTexture("SceneNormalVelocity");
        Texture* materialTexture = reg.getTexture("SceneMaterial");
        Texture* baseColorTexture = reg.getTexture("SceneBaseColor");
        Texture* bentNormalTexture = reg.getTexture("SceneBentNormal");

        return reg.createRenderTarget({ { RenderTarget::AttachmentType::Color0, colorTexture, loadOp, storeOp },
                                        { RenderTarget::AttachmentType::Color1, normalVelocityTexture, loadOp, storeOp },
                                        { RenderTarget::AttachmentType::Color2, bentNormalTexture, loadOp, storeOp },
                                        { RenderTarget::AttachmentType::Color3, materialTexture, loadOp, storeOp },
                                        { RenderTarget::AttachmentType::Color4, baseColorTexture, loadOp, storeOp },
                                        { RenderTarget::AttachmentType::Depth, depthTexture, loadOp, storeOp } });
    } else {
        ASSERT_NOT_REACHED();
    }
}

RenderState& ForwardRenderNode::makeForwardRenderState(Registry& reg, GpuScene const& scene, RenderTarget const& renderTarget, DrawKey const& drawKey) const
{
    std::vector<ShaderDefine> shaderDefines {};

    BlendMode blendMode = drawKey.blendMode().value();
    shaderDefines.push_back(ShaderDefine::makeInt("FORWARD_BLEND_MODE", blendModeToShaderBlendMode(blendMode)));

    bool doubleSided = drawKey.doubleSided().value();
    shaderDefines.push_back(ShaderDefine::makeBool("FORWARD_DOUBLE_SIDED", doubleSided));

    Shader shader = Shader::createBasicRasterize("forward/forward.vert", "forward/forward.frag", shaderDefines);

    VertexLayout vertexLayoutPos = scene.vertexManager().positionVertexLayout();
    VertexLayout vertexLayoutOther = scene.vertexManager().nonPositionVertexLayout();

    RenderStateBuilder renderStateBuilder { renderTarget, shader, { vertexLayoutPos, vertexLayoutOther } };

    renderStateBuilder.testDepth = true;
    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.writeDepth = blendMode != BlendMode::Translucent;

    renderStateBuilder.cullBackfaces = !doubleSided;

    if (m_mode == Mode::Translucent) {
        renderStateBuilder.stencilMode = StencilMode::Disabled;
    } else {
        // If we have a previous prepass ignore non-written stencil pixels. We always have to write something to the
        // stencil buffer, however, as the sky view shader relies on this test when drawing. Write bit1 for skin BRDF.
        renderStateBuilder.stencilMode = m_hasPreviousPrepass ? StencilMode::ReplaceIfGreaterOrEqual : StencilMode::AlwaysWrite;
        renderStateBuilder.stencilValue = 0x01;
        if (drawKey.brdf().has_value() && drawKey.brdf().value() == Brdf::Skin) {
            renderStateBuilder.stencilValue |= 0x02;
        }
    }

    Texture* dirLightProjectedShadow = reg.getTexture("DirectionalLightProjectedShadow");
    Texture* localLightShadowMapAtlas = reg.getTexture("LocalLightShadowMapAtlas");
    Buffer* localLightShadowAllocations = reg.getBuffer("LocalLightShadowAllocations");

    // Allow rendering without shadows
    if (!dirLightProjectedShadow || !localLightShadowMapAtlas || !localLightShadowAllocations) {
        Texture& placeholderTex = reg.createPixelTexture(vec4(1.0f), false);
        Buffer& placeholderBuffer = reg.createBufferForData(std::vector<int>(0), Buffer::Usage::StorageBuffer);
        placeholderBuffer.setStride(1); // add some non-zero stride just so that it won't complain, but it will likely generate some error on D3D12
        dirLightProjectedShadow = dirLightProjectedShadow ? dirLightProjectedShadow : &placeholderTex;
        localLightShadowMapAtlas = localLightShadowMapAtlas ? localLightShadowMapAtlas : &placeholderTex;
        localLightShadowAllocations = localLightShadowAllocations ? localLightShadowAllocations : &placeholderBuffer;
    }

    BindingSet& shadowBindingSet = reg.createBindingSet({ ShaderBinding::sampledTexture(*dirLightProjectedShadow),
                                                          ShaderBinding::sampledTexture(*localLightShadowMapAtlas),
                                                          ShaderBinding::storageBuffer(*localLightShadowAllocations) });

    StateBindings& bindings = renderStateBuilder.stateBindings();
    bindings.at(0, *reg.getBindingSet("SceneCameraSet"));
    bindings.at(2, *reg.getBindingSet("SceneObjectSet"));
    bindings.at(3, scene.globalMaterialBindingSet());
    bindings.at(4, *reg.getBindingSet("SceneLightSet"));
    bindings.at(5, shadowBindingSet);

    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName(fmt::format("Forward{}{}[doublesided={}][explicitvelocity={}]",
                                    magic_enum::enum_name(drawKey.blendMode().value()),
                                    magic_enum::enum_name(drawKey.brdf().value()),
                                    drawKey.doubleSided().value(),
                                    drawKey.hasExplicityVelocity().value()));

    return renderState;
}

std::vector<ForwardRenderNode::MeshSegmentInstance> ForwardRenderNode::generateSortedDrawList(GpuScene const& scene, Mode mode, ForwardMeshFilter meshFilter) const
{
    SCOPED_PROFILE_ZONE();

    std::vector<MeshSegmentInstance> meshSegmentInstances {};

    vec3 cameraPosition = scene.camera().position();
    //geometry::Frustum const& cameraFrustum = scene.camera().frustum();
    
    auto conditionallyAppendInstance = [&]<typename InstanceType>(InstanceType const& instance, StaticMesh const& mesh) -> void {

        constexpr u32 lodIdx = 0;
        StaticMeshLOD const& lod = mesh.lodAtIndex(lodIdx);

        // Early-out if we know there are no relevant segments
        if (mode == Mode::Translucent && !mesh.hasTranslucentSegments()) {
            return;
        } else if (mode == Mode::Opaque && !mesh.hasNonTranslucentSegments()) {
            return;
        }

        // TODO: Add me back! But probably AABB testing ...
        //if (not cameraFrustum.includesSphere(mesh.boundingSphere())) {
        //    return;
        //}

        for (u32 segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
            StaticMeshSegment const& meshSegment = lod.meshSegments[segmentIdx];

            if ((mode == Mode::Translucent && meshSegment.blendMode == BlendMode::Translucent)
                || (mode == Mode::Opaque && meshSegment.blendMode != BlendMode::Translucent)) {

                VertexAllocation vertexAllocation = meshSegment.vertexAllocation;
                DrawKey drawKey = meshSegment.drawKey;

                u32 drawableIdx = instance.drawableHandleForSegmentIndex(segmentIdx).template indexOfType<u32>();
                if constexpr (std::is_same_v<InstanceType, SkeletalMeshInstance>) {
                    if (instance.hasSkinningVertexMappingForSegmentIndex(segmentIdx)) {

                        // TODO/HACK: Don't modify it on the fly like this..
                        drawKey.setHasExplicityVelocity(true);

                        SkinningVertexMapping const& skinningVertexMapping = instance.skinningVertexMappingForSegmentIndex(segmentIdx);
                        meshSegmentInstances.emplace_back(skinningVertexMapping.skinnedTarget, drawKey, instance.transform(), drawableIdx);
                    }
                } else {
                    meshSegmentInstances.emplace_back(meshSegment.vertexAllocation, drawKey, instance.transform(), drawableIdx);
                }
            }
        }
    };

    bool includeStaticMeshes = meshFilter != ForwardMeshFilter::OnlySkeletalMeshes;
    bool includeSkeletalMeshes = meshFilter != ForwardMeshFilter::OnlyStaticMeshes;

    if (includeStaticMeshes) {
        for (auto const& instance : scene.staticMeshInstances()) {
            if (StaticMesh const* staticMesh = scene.staticMeshForInstance(*instance)) {
                conditionallyAppendInstance(*instance, *staticMesh);
            }
        }
    }

    if (includeSkeletalMeshes) {
        for (auto const& instance : scene.skeletalMeshInstances()) {
            if (SkeletalMesh const* skeletalMesh = scene.skeletalMeshForInstance(*instance)) {
                StaticMesh const& underlyingMesh = skeletalMesh->underlyingMesh();
                conditionallyAppendInstance(*instance, underlyingMesh);
            }
        }
    }

    if (mode == Mode::Translucent) {
        // Sort back to front
        std::sort(meshSegmentInstances.begin(), meshSegmentInstances.end(), [&](MeshSegmentInstance const& lhs, MeshSegmentInstance const& rhs) {
            float lhsDistance = distance(cameraPosition, lhs.transform->positionInWorld());
            float rhsDistance = distance(cameraPosition, rhs.transform->positionInWorld());
            return lhsDistance > rhsDistance;
        });
    } else {
        // Sort to minimize render state changes
        std::sort(meshSegmentInstances.begin(), meshSegmentInstances.end(), [&](MeshSegmentInstance const& lhs, MeshSegmentInstance const& rhs) {
            return lhs.drawKey.asUint32() < rhs.drawKey.asUint32();
        });
    }

    return meshSegmentInstances;
}
