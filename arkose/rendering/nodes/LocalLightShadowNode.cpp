#include "LocalLightShadowNode.h"

#include "core/math/Frustum.h"
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
}

RenderPipelineNode::ExecuteCallback LocalLightShadowNode::construct(GpuScene& scene, Registry& reg)
{
    Texture& shadowMapAtlas = reg.createTexture2D({ 4096, 4096 },
                                                  Texture::Format::Depth32F,
                                                  Texture::Filters::linear(),
                                                  Texture::Mipmap::None,
                                                  Texture::WrapModes::clampAllToEdge());
    reg.publish("LocalLightShadowMapAtlas", shadowMapAtlas);

    // TODO: Handle many lights!
    Buffer& shadowAllocationBuffer = reg.createBuffer(sizeof(vec4) * 32, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    reg.publish("LocalLightShadowAllocations", shadowAllocationBuffer);

    RenderTarget& atlasRenderTarget = reg.createRenderTarget({ { RenderTarget::AttachmentType::Depth, &shadowMapAtlas } });

    BindingSet& sceneObjectBindingSet = *reg.getBindingSet("SceneObjectSet");

    Shader shadowMapShader = Shader::createVertexOnly("shadow/biasedShadowMap.vert");
    RenderStateBuilder renderStateBuilder { atlasRenderTarget, shadowMapShader, m_vertexLayout };
    renderStateBuilder.stateBindings().at(0, sceneObjectBindingSet);
    RenderState& renderState = reg.createRenderState(renderStateBuilder);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        auto shadowMapClearValue = ClearValue::blackAtMaxDepth();

        if (m_maxNumShadowMaps == 0) {
            cmdList.clearTexture(shadowMapAtlas, shadowMapClearValue);
            return;
        }

        std::vector<ShadowMapAtlasAllocation> shadowMapAllocations = allocateShadowMapsInAtlas(scene, shadowMapAtlas);
        if (shadowMapAllocations.empty()) {
            cmdList.clearTexture(shadowMapAtlas, shadowMapClearValue);
            return;
        }

        std::vector<vec4> shadowMapViewports = collectAtlasViewportDataForAllocations(scene, shadowMapAtlas.extent(), shadowMapAllocations);
        uploadBuffer.upload(shadowMapViewports, shadowAllocationBuffer);
        cmdList.executeBufferCopyOperations(uploadBuffer);

        // NOTE: We assume that all or most meshes will be drawn in a shadow map so we prepare all of them
        scene.ensureDrawCallIsAvailableForAll(m_vertexLayout);

        cmdList.beginRendering(renderState, shadowMapClearValue);

        cmdList.bindVertexBuffer(scene.globalVertexBufferForLayout(m_vertexLayout));
        cmdList.bindIndexBuffer(scene.globalIndexBuffer(), scene.globalIndexBufferType());

        for (ShadowMapAtlasAllocation& shadowMapAllocation : shadowMapAllocations) {
            Light::Type lightType = shadowMapAllocation.light->type();
            switch (lightType) {
            case Light::Type::SpotLight:
                drawSpotLightShadowMap(cmdList, scene, shadowMapAllocation);
                break;
            case Light::Type::PointLight:
                NOT_YET_IMPLEMENTED();
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

        float distance = ark::distance(scene.camera().position(), light.position());

        float coneAngle = ark::TWO_PI;
        if (light.type() == Light::Type::SpotLight) {
            auto& spotLight = static_cast<const SpotLight&>(light);
            coneAngle = spotLight.outerConeAngle;
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

    std::string zoneName = fmt::format(FMT_STRING("Light [{}]"), light.name());
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

void LocalLightShadowNode::drawShadowCasters(CommandList& cmdList, GpuScene& scene, geometry::Frustum& lightFrustum) const
{
    // TODO: Use GPU based culling

    uint32_t drawIdx = 0;
    for (auto& instance : scene.scene().staticMeshInstances()) {
        if (const StaticMesh* staticMesh = scene.staticMeshForHandle(instance->mesh)) {

            // TODO: Pick LOD properly
            const StaticMeshLOD& lod = staticMesh->lodAtIndex(0);

            geometry::Sphere sphere = lod.boundingSphere.transformed(instance->transform.worldMatrix());
            if (lightFrustum.includesSphere(sphere)) {

                for (const StaticMeshSegment& meshSegment : lod.meshSegments) {

                    // Don't render translucent objects. We still do masked though and pretend they are opaque. This may fail
                    // in some cases but in general if the masked features are small enough it's not really noticable.
                    if (const Material* material = scene.materialForHandle(meshSegment.material)) {
                        if (material->blendMode == Material::BlendMode::Translucent) {
                            break;
                        }
                    }

                    DrawCallDescription drawCall = meshSegment.drawCallDescription(m_vertexLayout, scene);
                    drawCall.firstInstance = drawIdx++; // TODO: Put this in some buffer instead!

                    cmdList.issueDrawCall(drawCall);
                }
            }
        }
    }
}
