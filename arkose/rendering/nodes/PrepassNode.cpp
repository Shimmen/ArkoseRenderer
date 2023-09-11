#include "PrepassNode.h"

#include "rendering/util/ScopedDebugZone.h"
#include <imgui.h>

PrepassNode::PrepassNode(ForwardMeshFilter meshFilter, ForwardClearMode clearMode)
    : m_meshFilter(meshFilter)
    , m_clearMode(clearMode)
{
}

RenderPipelineNode::ExecuteCallback PrepassNode::construct(GpuScene& scene, Registry& reg)
{
    // Create render target

    Texture& sceneDepth = *reg.getTexture("SceneDepth");
    RenderTarget& renderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &sceneDepth, LoadOp::Load, StoreOp::Store } });

    // Create all render states (PSOs) needed for rendering

    auto& renderStateLookup = reg.allocate<std::unordered_map<u32, RenderState*>>();

    auto stateDrawKeys = { DrawKey({}, BlendMode::Opaque, false, {}),
                           DrawKey({}, BlendMode::Opaque, true, {}),
                           DrawKey({}, BlendMode::Masked, false, {}),
                           DrawKey({}, BlendMode::Masked, true, {}) };

    for (DrawKey const& drawKey : stateDrawKeys) {
        renderStateLookup[drawKey.asUint32()] = &makeRenderState(reg, scene, renderTarget, drawKey);
    }

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        if (m_clearMode == ForwardClearMode::ClearBeforeFirstDraw) {
            cmdList.clearTexture(sceneDepth, ClearValue::blackAtMaxDepth());
        }

        std::vector<MeshSegmentInstance> instances = generateDrawList(scene, m_meshFilter);
        if (instances.empty()) {
            return;
        }

        cmdList.bindVertexBuffer(scene.vertexManager().positionVertexBuffer(), 0);
        cmdList.bindVertexBuffer(scene.vertexManager().nonPositionVertexBuffer(), 1);
        cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

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

                cmdList.setNamedUniform("depthOffset", 0.00005f);
                cmdList.setNamedUniform("projectionFromWorld", scene.camera().viewProjectionMatrix());

                currentStateDrawKey = &instance.drawKey;
            }

            DrawCallDescription drawCall = instance.vertexAllocation.asDrawCallDescription();
            drawCall.firstInstance = instance.drawableIdx;
            cmdList.issueDrawCall(drawCall);

            firstDraw = false;
        }

        cmdList.endRendering();
        cmdList.endDebugLabel();
    };
}

PrepassNode::MeshSegmentInstance::MeshSegmentInstance(VertexAllocation inVertexAllocation, DrawKey inDrawKey, u32 inDrawableIdx)
    : vertexAllocation(inVertexAllocation)
    , drawKey(inDrawKey)
    , drawableIdx(inDrawableIdx)
{
}

RenderState& PrepassNode::makeRenderState(Registry& reg, GpuScene const& scene, RenderTarget const& renderTarget, DrawKey const& drawKey) const
{
    bool doubleSided = drawKey.doubleSided().value();
    BlendMode blendMode = drawKey.blendMode().value();

    Shader shader {};
    std::vector<VertexLayout> vertexLayout {};

    switch (blendMode) {
    case BlendMode::Opaque:
        shader = Shader::createVertexOnly("forward/prepass.vert");
        vertexLayout = { scene.vertexManager().positionVertexLayout() };
        break;
    case BlendMode::Masked:
        shader = Shader::createBasicRasterize("forward/prepassMasked.vert", "forward/prepassMasked.frag");
        vertexLayout = { scene.vertexManager().positionVertexLayout(), scene.vertexManager().nonPositionVertexLayout() };
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    RenderStateBuilder renderStateBuilder { renderTarget, shader, std::move(vertexLayout) };
    renderStateBuilder.testDepth = true;
    renderStateBuilder.depthCompare = DepthCompareOp::LessThanEqual;
    renderStateBuilder.cullBackfaces = !doubleSided;
    renderStateBuilder.stencilMode = StencilMode::AlwaysWrite;

    renderStateBuilder.stateBindings().at(0, *reg.getBindingSet("SceneObjectSet"));
    if (blendMode == BlendMode::Masked) {
        renderStateBuilder.stateBindings().at(1, scene.globalMaterialBindingSet());
    }

    RenderState& renderState = reg.createRenderState(renderStateBuilder);
    renderState.setName(fmt::format("Prepass{}[doublesided={}]", BlendModeName(blendMode), doubleSided));

    return renderState;
}

std::vector<PrepassNode::MeshSegmentInstance> PrepassNode::generateDrawList(GpuScene const& scene, ForwardMeshFilter meshFilter) const
{
    SCOPED_PROFILE_ZONE();

    std::vector<MeshSegmentInstance> meshSegmentInstances {};

    vec3 cameraPosition = scene.camera().position();
    geometry::Frustum const& cameraFrustum = scene.camera().frustum();

    auto conditionallyAppendInstance = [&]<typename InstanceType>(InstanceType const& instance, StaticMesh const& mesh) -> void {
        constexpr u32 lodIdx = 0;
        StaticMeshLOD const& lod = mesh.lodAtIndex(lodIdx);

        // Early-out if we know there are no relevant segments
        if (!mesh.hasNonTranslucentSegments()) {
            return;
        }

        // TODO: Add me back! But probably AABB testing ...
        // if (not cameraFrustum.includesSphere(mesh.boundingSphere())) {
        //    return;
        //}

        for (u32 segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
            StaticMeshSegment const& meshSegment = lod.meshSegments[segmentIdx];

            if (meshSegment.blendMode != BlendMode::Translucent) {

                // Construct suitable draw key
                // This is a bit ugly, but it works for now..
                BlendMode blendMode = meshSegment.drawKey.blendMode().value();
                bool doubleSided = meshSegment.drawKey.doubleSided().value();
                DrawKey prepassDrawKey = DrawKey({}, blendMode, doubleSided, {});

                VertexAllocation vertexAllocation = meshSegment.vertexAllocation;
                if constexpr (std::is_same_v<InstanceType, SkeletalMeshInstance>) {
                    SkinningVertexMapping const& skinningVertexMapping = instance.skinningVertexMappingForSegmentIndex(segmentIdx);
                    vertexAllocation = skinningVertexMapping.skinnedTarget;
                }

                u32 drawableIdx = instance.drawableHandleForSegmentIndex(segmentIdx).indexOfType<u32>();
                meshSegmentInstances.emplace_back(vertexAllocation, prepassDrawKey, drawableIdx);
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

    return meshSegmentInstances;
}
