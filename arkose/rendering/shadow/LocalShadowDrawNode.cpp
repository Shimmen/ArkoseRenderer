#include "LocalShadowDrawNode.h"

#include "core/math/Frustum.h"
#include "core/parallel/ParallelFor.h"
#include "rendering/GpuScene.h"
#include "scene/lights/Light.h"
#include "scene/lights/SpotLight.h"
#include "rendering/util/ScopedDebugZone.h"
#include "utility/Profiling.h"
#include <ark/rect.h>
#include <fmt/format.h>
#include <imgui.h>

void LocalShadowDrawNode::drawGui()
{
    MeshletDepthOnlyRenderNode::drawGui();
    ImGui::Separator();

    ImGui::SliderInt("Max number of shadow maps", &m_maxNumShadowMaps, 0, 32);
    drawTextureVisualizeGui(*m_shadowMapAtlas);
}

RenderPipelineNode::ExecuteCallback LocalShadowDrawNode::construct(GpuScene& scene, Registry& reg)
{
    //
    // TODO: Move all of the shadow map atlas allocation & priority stuff to the GpuScene, or something like that.
    // I think this should only be responsible for actually drawing. Eventually we'll also likely want to do some
    // lights with ray traced shadows, so then we'd only want to draw the ones with shadow maps here, while the
    // ray traced ones have its own path. And for that we need some manager which sits above us here.
    //

    m_shadowMapAtlas = &reg.createTexture2D({ 4096, 4096 },
                                            Texture::Format::Depth32F,
                                            Texture::Filters::linear(),
                                            Texture::Mipmap::None,
                                            ImageWrapModes::clampAllToEdge());
    reg.publish("LocalLightShadowMapAtlas", *m_shadowMapAtlas);

    // TODO: Handle many lights! (more than 32)
    Buffer& shadowAllocationBuffer = reg.createBuffer(sizeof(vec4) * 32, Buffer::Usage::StorageBuffer);
    shadowAllocationBuffer.setStride(sizeof(vec4));
    reg.publish("LocalLightShadowAllocations", shadowAllocationBuffer);

    std::vector<RenderStateWithIndirectData*> const& renderStates = createRenderStates(reg, scene);

    std::vector<MeshletIndirectBuffer*> indirectBuffers {};
    for (auto const& renderState : renderStates) {
        indirectBuffers.push_back(renderState->indirectBuffer);
    }
    MeshletIndirectSetupState const& indirectSetupState = m_meshletIndirectHelper.createMeshletIndirectSetupState(reg, indirectBuffers);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        auto shadowMapClearValue = ClearValue::blackAtMaxDepth();

        // Useful for debugging to avoid having to see stale shadow maps in the atlas.
        // But in the future it might be better to clear the allocations as needed.
        // Care has to be taken to ensure we never sample from a stale shadow map
        // though, etc. so it takes a little more care than just clearing it here..
        cmdList.clearTexture(*m_shadowMapAtlas, shadowMapClearValue);

        if (m_maxNumShadowMaps == 0) {
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

        for (ShadowMapAtlasAllocation& shadowMapAllocation : shadowMapAllocations) {
            Light const& light = *shadowMapAllocation.light;

            // TODO: Also handle sphere lights, maybe, but we might have them be ray traced only..
            ARKOSE_ASSERT(light.type() == Light::Type::SpotLight);

            std::string zoneName = fmt::format("Light [{}]", light.name());
            ScopedDebugZone zone { cmdList, zoneName };

            m_meshletIndirectHelper.executeMeshletIndirectSetup(scene, cmdList, uploadBuffer, indirectSetupState, {});

            mat4 projectionFromWorld = light.viewProjection();

            geometry::Frustum cullingFrustum = geometry::Frustum::createFromProjectionMatrix(projectionFromWorld);
            size_t frustumPlaneDataSize;
            void const* frustumPlaneData = reinterpret_cast<void const*>(cullingFrustum.rawPlaneData(&frustumPlaneDataSize));

            Rect2D viewportRect = shadowMapAllocation.rect;
            cmdList.setViewport(viewportRect.origin, viewportRect.size);

            for (RenderStateWithIndirectData* renderState : renderStates) {

                cmdList.beginRendering(*renderState->renderState, false);
                cmdList.setDepthBias(light.constantBias(), light.slopeBias());

                cmdList.setNamedUniform("projectionFromWorld", projectionFromWorld);
                cmdList.setNamedUniform("frustumPlanes", frustumPlaneData, frustumPlaneDataSize);
                cmdList.setNamedUniform("frustumCullMeshlets", m_frustumCullMeshlets);

                MeshletIndirectBuffer& indirectBuffer = *renderState->indirectBuffer;
                m_meshletIndirectHelper.drawMeshletsWithIndirectBuffer(cmdList, indirectBuffer);

                cmdList.endRendering();
            }
        }
    };
}

std::vector<LocalShadowDrawNode::ShadowMapAtlasAllocation> LocalShadowDrawNode::allocateShadowMapsInAtlas(const GpuScene& scene, const Texture& atlas) const
{
    SCOPED_PROFILE_ZONE();

    std::vector<const Light*> shadowCastingLights {};
    scene.forEachLocalLight([&](size_t, const Light& light) {
        if (light.shadowMode() == ShadowMode::ShadowMapped) {
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

std::vector<vec4> LocalShadowDrawNode::collectAtlasViewportDataForAllocations(const GpuScene& scene, Extent2D atlasExtent, const std::vector<ShadowMapAtlasAllocation>& shadowMapAllocations) const
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

RenderTarget& LocalShadowDrawNode::makeRenderTarget(Registry& reg, LoadOp loadOp) const
{
    // Ignore the supplied load-op, we instead clear the texture manually then always load for the render passes
    (void)loadOp;

    return reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, m_shadowMapAtlas, LoadOp::Load, StoreOp::Store } });
}
