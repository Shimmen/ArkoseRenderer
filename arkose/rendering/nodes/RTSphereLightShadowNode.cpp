#include "RTSphereLightShadowNode.h"

#include "core/math/Frustum.h"
#include "rendering/GpuScene.h"
#include "rendering/util/ScopedDebugZone.h"
#include "scene/lights/Light.h"
#include "utility/Profiling.h"
#include <imgui.h>

void RTSphereLightShadowNode::drawGui()
{
}

RenderPipelineNode::ExecuteCallback RTSphereLightShadowNode::construct(GpuScene& scene, Registry& reg)
{
    if (!(reg.hasPreviousNode("Prepass") || reg.hasPreviousNode("Forward"))) {
        ARKOSE_LOG_FATAL("Sphere light shadow needs scene depth information, can't progress");
    }

    Texture& sceneDepth = *reg.getTexture("SceneDepth");
    Buffer& cameraDataBuffer = *reg.getBuffer("SceneCameraData");
    //Texture& blueNoiseTexArray = *reg.getTexture("BlueNoise");

    Texture& projectedShadowTex = reg.createTexture2D(reg.windowRenderTarget().extent(), Texture::Format::R8);
    reg.publish("SphereLightProjectedShadow", projectedShadowTex);

    TopLevelAS& sceneTLAS = scene.globalTopLevelAccelerationStructure();
    BindingSet& bindingSet = reg.createBindingSet({ ShaderBinding::topLevelAccelerationStructure(sceneTLAS, ShaderStage::RTRayGen),
                                                    ShaderBinding::constantBuffer(cameraDataBuffer, ShaderStage::RTRayGen),
                                                    ShaderBinding::sampledTexture(sceneDepth, ShaderStage::RTRayGen),
                                                    ShaderBinding::storageTexture(projectedShadowTex, ShaderStage::RTRayGen) });

    ShaderFile raygen { "rt-shadow/raygen.rgen" };
    ShaderFile shadowMissShader { "rayTracing/common/shadow.rmiss" };
    ShaderBindingTable sbt { raygen, { /* no hit groups */ }, { shadowMissShader } };

    StateBindings stateDataBindings;
    stateDataBindings.at(0, bindingSet);

    constexpr uint32_t maxRecursionDepth = 1; // raygen -> shadow ray
    RayTracingState& rtState = reg.createRayTracingState(sbt, stateDataBindings, maxRecursionDepth);

    return [&](const AppState& appState, CommandList& cmdList, UploadBuffer& uploadBuffer) {

        cmdList.clearTexture(projectedShadowTex, ClearValue::blackAtMaxDepth());

        //mat4 cameraViewProjection = scene.camera().viewProjectionMatrix();
        //auto cameraFrustum = geometry::Frustum::createFromProjectionMatrix(cameraViewProjection);

        scene.forEachShadowCastingLight([&](size_t lightIdx, Light const& light) {

            if (light.type() != Light::Type::SphereLight) {
                return;
            }

            SphereLight const& sphereLight = static_cast<SphereLight const&>(light);

            // TODO: Define a radius for the light falloff!
            //if (not light.castLightsIntoFrustum(cameraFrustum)) {
            //    return;
            //}

            // ..

            ScopedDebugZone zone { cmdList, "Sphere Light" };

            cmdList.setRayTracingState(rtState);

            vec3 lightPosition = sphereLight.transform().positionInWorld();
            cmdList.setNamedUniform("parameter1", lightPosition.x);
            cmdList.setNamedUniform("parameter2", lightPosition.y);
            cmdList.setNamedUniform("parameter3", lightPosition.z);
            cmdList.setNamedUniform("parameter4", sphereLight.lightSourceRadius);

            // TODO: Limit to the radius/influence of the light source onto the world
            cmdList.traceRays(appState.windowExtent());

        });
    };
}
