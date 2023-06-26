#include "LocalLightShadowNode.h"

#include "core/math/Frustum.h"
#include "core/parallel/ParallelFor.h"
#include "rendering/GpuScene.h"
#include "scene/lights/Light.h"
#include "scene/lights/SpotLight.h"
#include "rendering/util/ScopedDebugZone.h"
#include "utility/Profiling.h"
#include <ark/rect.h>
#include <imgui.h>

void LocalLightShadowNode::drawGui()
{
    ImGui::SliderInt("Max number of shadow maps", &m_maxNumShadowMaps, 0, 32);
    drawTextureVisualizeGui(*m_shadowMapAtlas);
}

RenderPipelineNode::ExecuteCallback LocalLightShadowNode::construct(GpuScene& scene, Registry& reg)
{
    m_shadowMapAtlas = &reg.createTexture2D({ 4096, 4096 },
                                            Texture::Format::Depth32F,
                                            Texture::Filters::linear(),
                                            Texture::Mipmap::None,
                                            ImageWrapModes::clampAllToEdge());
    reg.publish("LocalLightShadowMapAtlas", *m_shadowMapAtlas);

    // TODO: Handle many lights!
    Buffer& shadowAllocationBuffer = reg.createBuffer(sizeof(vec4) * 32, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("LocalLightShadowAllocations", shadowAllocationBuffer);

    RenderTarget& atlasRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, m_shadowMapAtlas } });

    BindingSet& sceneObjectBindingSet = *reg.getBindingSet("SceneObjectSet");

    Shader shadowMapShader = Shader::createVertexOnly("shadow/biasedShadowMap.vert");

    VertexLayout vertexLayoutPos = scene.vertexManager().positionVertexLayout();
    VertexLayout vertexLayoutOther = scene.vertexManager().nonPositionVertexLayout();

    RenderStateBuilder renderStateBuilder { atlasRenderTarget, shadowMapShader, { vertexLayoutPos, vertexLayoutOther } };
    renderStateBuilder.stateBindings().at(0, sceneObjectBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        auto shadowMapClearValue = ClearValue::blackAtMaxDepth();

        if (m_maxNumShadowMaps == 0) {
            cmdList.clearTexture(*m_shadowMapAtlas, shadowMapClearValue);
            return;
        }

        std::vector<ShadowMapAtlasAllocation> shadowMapAllocations = allocateShadowMapsInAtlas(scene, *m_shadowMapAtlas);
        if (shadowMapAllocations.empty()) {
            cmdList.clearTexture(*m_shadowMapAtlas, shadowMapClearValue);
            return;
        }

        std::vector<vec4> shadowMapViewports = collectAtlasViewportDataForAllocations(scene, m_shadowMapAtlas->extent(), shadowMapAllocations);
        uploadBuffer.upload(shadowMapViewports, shadowAllocationBuffer);
        cmdList.executeBufferCopyOperations(uploadBuffer);

        cmdList.beginRendering(renderState, shadowMapClearValue);
        cmdList.bindVertexBuffer(scene.vertexManager().positionVertexBuffer(), 0);
        cmdList.bindVertexBuffer(scene.vertexManager().nonPositionVertexBuffer(), 1);
        cmdList.bindIndexBuffer(scene.vertexManager().indexBuffer(), scene.vertexManager().indexType());

        for (ShadowMapAtlasAllocation& shadowMapAllocation : shadowMapAllocations) {
            Light::Type lightType = shadowMapAllocation.light->type();
            switch (lightType) {
            case Light::Type::SpotLight:
                drawSpotLightShadowMap(cmdList, scene, shadowMapAllocation);
                break;
            case Light::Type::SphereLight:
                //NOT_YET_IMPLEMENTED();
                break;
            }
        }

        cmdList.endRendering();
    };
}

std::vector<LocalLightShadowNode::ShadowMapAtlasAllocation> LocalLightShadowNode::allocateShadowMapsInAtlas(const GpuScene& scene, const Texture& atlas) const
{
    SCOPED_PROFILE_ZONE();

    std::vector<const Light*> shadowCastingLights {};
    scene.forEachShadowCastingLight([&](size_t, const Light& light) {
        if (light.castsShadows() && light.type() != Light::Type::DirectionalLight) {
            shadowCastingLights.push_back(&light);
        }
    });

    std::vector<ShadowMapAtlasAllocation> allocations {};
    allocations.reserve(m_maxNumShadowMaps);

    if (shadowCastingLights.empty()) {
        return allocations;
    }

    if (!ark::isPowerOfTwo(atlas.extent().width()) || !ark::isPowerOfTwo(atlas.extent().height())) {
        ARKOSE_LOG(Warning, "Shadow map atlas texture does not have a power-of-two size, which is optimal for our subdivision strategy.");
    }

    // Performance: consider memoizing/caching importance values for lights
    auto calculateLightImportance = [&](const Light& light) -> float {

        float distance = ark::distance(scene.camera().position(), light.transform().positionInWorld());

        float coneAngle = ark::TWO_PI;
        if (light.type() == Light::Type::SpotLight) {
            auto& spotLight = static_cast<const SpotLight&>(light);
            coneAngle = spotLight.outerConeAngle();
        }

        return (1.0f / distance) * (coneAngle / ark::TWO_PI);
    };

    std::stable_sort(shadowCastingLights.begin(), shadowCastingLights.end(), [&](const Light* lhs, const Light* rhs) -> bool {
        return calculateLightImportance(*lhs) > calculateLightImportance(*rhs);
    });

    // Keep only the n most important
    while (shadowCastingLights.size() > m_maxNumShadowMaps) {
        shadowCastingLights.pop_back();
    }

    int nextLightIdx = 0;
    auto addNextLightWithRect = [&](Rect2D rect) -> bool {
        allocations.push_back({ .light = shadowCastingLights[nextLightIdx++],
                                .rect = rect });
        return nextLightIdx < shadowCastingLights.size();
    };

    Rect2D fullAtlasRect { atlas.extent().asIntVector() };
    Rect2D baseRect = fullAtlasRect;

    ARK_ASSERT(shadowCastingLights.size() >= 1);

    while (true) {

        // NOTE: We intentionally swap top/bottom here since we go from a bottom-left (maths) to a top-left (texture) coordinate system
        Rect2D bl, br, tl, tr;
        bool subdivideSuccess = baseRect.subdivideWithBorder(tl, tr, bl, br, 1u);

        if (!subdivideSuccess || any(lessThan(bl.size, m_minimumViableShadowMapSize))) {
            ARKOSE_LOG(Warning, "Can't subdivide rect to fit all local shadow maps we want, so some will be without. "
                                "Increase the shadow atlas resolution to be able fit more shadow maps.");
        }

        // This could be shortened with a macro, but it's not really a problem for these few 3 duplications..

        bool continueAdding = true;

        continueAdding = addNextLightWithRect(tl);
        if (continueAdding == false) {
            break;
        }

        continueAdding = addNextLightWithRect(tr);
        if (continueAdding == false) {
            break;
        }

        continueAdding = addNextLightWithRect(bl);
        if (continueAdding == false) {
            break;
        }

        if (nextLightIdx == shadowCastingLights.size() - 1) {
            continueAdding = addNextLightWithRect(br);
            ARK_ASSERT(continueAdding == false);
            break;
        } else {
            baseRect = br;
        }
    }

    return allocations;
}

std::vector<vec4> LocalLightShadowNode::collectAtlasViewportDataForAllocations(const GpuScene& scene, Extent2D atlasExtent, const std::vector<ShadowMapAtlasAllocation>& shadowMapAllocations) const
{
    SCOPED_PROFILE_ZONE();

    std::vector<vec4> viewports {};

    scene.forEachLocalLight([&](size_t, const Light& light) {

        vec4 viewport = vec4(0, 0, 0, 0);

        if (light.castsShadows()) {
            // Performance: this won't scale very well with many lights.. (still O(n) w.r.t. total light count though)
            for (const ShadowMapAtlasAllocation& allocation : shadowMapAllocations) {
                if (allocation.light == &light) {

                    float x = static_cast<float>(allocation.rect.origin.x) / atlasExtent.width();
                    float y = static_cast<float>(allocation.rect.origin.y) / atlasExtent.height();
                    float w = static_cast<float>(allocation.rect.size.x) / atlasExtent.width();
                    float h = static_cast<float>(allocation.rect.size.y) / atlasExtent.height();

                    viewport = vec4(x, y, w, h);
                }
            }
        }

        viewports.push_back(viewport);
    });

    return viewports;
}

void LocalLightShadowNode::drawSpotLightShadowMap(CommandList& cmdList, GpuScene& scene, const ShadowMapAtlasAllocation& shadowMapAllocation) const
{
    SCOPED_PROFILE_ZONE();

    ARK_ASSERT(shadowMapAllocation.light);
    ARK_ASSERT(shadowMapAllocation.light->type() == Light::Type::SpotLight);
    const Light& light = *shadowMapAllocation.light;

    std::string zoneName = std::format("Light [{}]", light.name());
    ScopedDebugZone zone { cmdList, zoneName };

    mat4 lightProjectionFromWorld = light.viewProjection();
    auto lightFrustum = geometry::Frustum::createFromProjectionMatrix(lightProjectionFromWorld);

    Extent2D effectiveShadowMapExtent = { shadowMapAllocation.rect.size.x, shadowMapAllocation.rect.size.y };

    cmdList.setNamedUniform<mat4>("lightProjectionFromWorld", lightProjectionFromWorld);
    cmdList.setNamedUniform<vec3>("worldLightDirection", light.forwardDirection());
    cmdList.setNamedUniform<float>("constantBias", light.constantBias(effectiveShadowMapExtent));
    cmdList.setNamedUniform<float>("slopeBias", light.slopeBias(effectiveShadowMapExtent));

    Rect2D viewportRect = shadowMapAllocation.rect;
    cmdList.setViewport(viewportRect.origin, viewportRect.size);

    drawShadowCasters(cmdList, scene, lightFrustum);
}

void LocalLightShadowNode::drawShadowCasters(CommandList& cmdList, GpuScene& scene, geometry::Frustum const& lightFrustum) const
{
    // TODO: Use GPU based culling

    moodycamel::ConcurrentQueue<DrawCallDescription> drawCalls {};

    auto& instances = scene.staticMeshInstances();
    ParallelForBatched(instances.size(), 64, [&](size_t idx) {
        auto& instance = instances[idx];

        if (StaticMesh const* staticMesh = scene.staticMeshForInstance(*instance)) {

            if (!staticMesh->hasNonTranslucentSegments()) {
                return;
            }

            // TODO: Pick LOD properly
            const StaticMeshLOD& lod = staticMesh->lodAtIndex(0);

            ark::aabb3 aabb = staticMesh->boundingBox().transformed(instance->transform().worldMatrix());
            if (lightFrustum.includesAABB(aabb)) {

                for (u32 segmentIdx = 0; segmentIdx < lod.meshSegments.size(); ++segmentIdx) {
                    StaticMeshSegment const& meshSegment = lod.meshSegments[segmentIdx];

                    // Don't render translucent objects. We still do masked though and pretend they are opaque. This may fail
                    // in some cases but in general if the masked features are small enough it's not really noticable.
                    if (meshSegment.blendMode == BlendMode::Translucent) {
                        continue;
                    }

                    DrawCallDescription drawCall = meshSegment.vertexAllocation.asDrawCallDescription();
                    drawCall.firstInstance = instance->drawableHandleForSegmentIndex(segmentIdx).indexOfType<u32>(); // TODO: Put this in some buffer instead!

                    drawCalls.enqueue(drawCall);
                }
            }
        }
    });

    DrawCallDescription drawCall;
    while (drawCalls.try_dequeue(drawCall)) {
        cmdList.issueDrawCall(drawCall);
    }
}
