#include "SceneNode.h"

#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <imgui.h>
#include <moos/vector.h>
#include <unordered_map>

// Shader shader headers
using uint = uint32_t;
#include "LightData.h"
#include "CameraState.h"

std::string SceneNode::name()
{
    return "scene";
}

SceneNode::SceneNode(Scene& scene)
    : RenderGraphNode(SceneNode::name())
    , m_scene(scene)
{
}

RenderGraphNode::ExecuteCallback SceneNode::constructFrame(Registry& reg) const
{
    SCOPED_PROFILE_ZONE();

    Buffer& cameraBuffer = reg.createBuffer(sizeof(CameraState), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::GpuOnly);
    cameraBuffer.setName("SceneCameraData");
    BindingSet& cameraBindingSet = reg.createBindingSet({ { 0, ShaderStageAnyRasterize, &cameraBuffer } });
    reg.publish("camera", cameraBuffer);
    reg.publish("cameraSet", cameraBindingSet);

    // Environment mapping stuff
    Texture& envTexture = m_scene.environmentMap().empty()
        ? reg.createPixelTexture(vec4(1.0f), true)
        : reg.loadTexture2D(m_scene.environmentMap(), true, false);
    envTexture.setName("SceneEnvironmentTexture");
    reg.publish("environmentMap", envTexture);

    // Object data stuff
    // TODO: Resize the buffer if needed when more meshes are added
    size_t objectDataBufferSize = m_scene.meshCount() * sizeof(ShaderDrawable);
    Buffer& objectDataBuffer = reg.createBuffer(objectDataBufferSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    objectDataBuffer.setName("SceneObjectData");
    reg.publish("objectData", objectDataBuffer);

    BindingSet& objectBindingSet = reg.createBindingSet({ { 0, ShaderStageVertex, &objectDataBuffer } });
    reg.publish("objectSet", objectBindingSet);

    // Light shadow data stuff
    Buffer& lightShadowDataBuffer = reg.createBuffer(SCENE_MAX_SHADOW_MAPS * sizeof(PerLightShadowData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    lightShadowDataBuffer.setName("SceneShadowData");
    reg.publish("shadowData", lightShadowDataBuffer);

    // Light data stuff
    Buffer& lightMetaDataBuffer = reg.createBuffer(sizeof(LightMetaData), Buffer::Usage::UniformBuffer, Buffer::MemoryHint::GpuOnly);
    lightMetaDataBuffer.setName("SceneLightMetaData");
    Buffer& dirLightDataBuffer = reg.createBuffer(sizeof(DirectionalLightData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    dirLightDataBuffer.setName("SceneDirectionalLightData");
    Buffer& spotLightDataBuffer = reg.createBuffer(10 * sizeof(SpotLightData), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    spotLightDataBuffer.setName("SceneSpotLightData");
    std::vector<Texture*> iesProfileLUTs;
    std::vector<Texture*> shadowMaps;
    // TODO: We need to be able to update the shadow map binding. Right now we can only do it once, at creation.
    m_scene.forEachLight([&](size_t, Light& light) {
        if (light.type() == Light::Type::SpotLight)
            iesProfileLUTs.push_back(&((SpotLight&)light).iesProfileLookupTexture()); // all this light stuff needs cleanup...
        if (light.castsShadows())
            shadowMaps.push_back(&light.shadowMap());
    });
    BindingSet& lightBindingSet = reg.createBindingSet({ { 0, ShaderStageFragment, &lightMetaDataBuffer },
                                                         { 1, ShaderStageFragment, &dirLightDataBuffer },
                                                         { 2, ShaderStageFragment, &spotLightDataBuffer },
                                                         { 3, ShaderStageFragment, shadowMaps, SCENE_MAX_SHADOW_MAPS },
                                                         { 4, ShaderStageFragment, iesProfileLUTs, SCENE_MAX_IES_LUT } });
    reg.publish("lightSet", lightBindingSet);

    return [&](const AppState& appState, CommandList& cmdList) {

        UploadBuffer& uploadBuffer = reg.getUploadBuffer();

        // Update camera data
        {
            const FpsCamera& camera = m_scene.camera();
            mat4 pixelFromView = camera.pixelProjectionMatrix();
            mat4 projectionFromView = camera.projectionMatrix();
            mat4 viewFromWorld = camera.viewMatrix();
            CameraState cameraState {
                .projectionFromView = projectionFromView,
                .viewFromProjection = inverse(projectionFromView),
                .viewFromWorld = viewFromWorld,
                .worldFromView = inverse(viewFromWorld),

                .pixelFromView = pixelFromView,
                .viewFromPixel = inverse(pixelFromView),

                .near = camera.zNear,
                .far = camera.zFar,

                .iso = camera.iso,
                .aperture = camera.aperture,
                .shutterSpeed = camera.shutterSpeed,
                .exposureCompensation = camera.exposureCompensation,
            };
            uploadBuffer.upload(cameraState, cameraBuffer);
        }

        // Update object data
        {
            std::vector<ShaderDrawable> objectData {};
            m_scene.forEachMesh([&](size_t, Mesh& mesh) {
                objectData.push_back(ShaderDrawable { .worldFromLocal = mesh.transform().worldMatrix(),
                                                      .worldFromTangent = mat4(mesh.transform().worldNormalMatrix()),
                                                      .materialIndex = mesh.materialIndex().value_or(0) });
            });
            uploadBuffer.upload(objectData, objectDataBuffer);
        }

        // Update exposure data
        {
            // TODO: hmm, I kinda wanna do this in Scene, not scene node.. this whole Scene/SceneNode split is stupid.

            if (m_scene.camera().useAutomaticExposure) {
                if (m_scene.isNextFrameExposureResultBufferReady({})) {
                    float lastFrameExposure;
                    cmdList.slowBlockingReadFromBuffer(*m_scene.popNextFrameExposureResultBuffer({}), 0, sizeof(lastFrameExposure), &lastFrameExposure);
                    m_scene.setLightPreExposureValue({}, lastFrameExposure);
                } else {
                    // Let's just not touch the variable, it's probably going to be ready next frame anyway
                }
            } else {
                // See camera.glsl for reference
                auto& camera = m_scene.camera();
                float ev100 = std::log2f((camera.aperture * camera.aperture) / camera.shutterSpeed * 100.0f / camera.iso);
                float maxLuminance = 1.2f * std::pow(2.0f, ev100);
                m_scene.setLightPreExposureValue({}, 1.0f / maxLuminance);
            }
        }

        // Update light data
        {
            mat4 viewFromWorld = m_scene.camera().viewMatrix();
            mat4 worldFromView = inverse(viewFromWorld);

            int nextShadowMapIndex = 0;
            std::vector<DirectionalLightData> dirLightData;
            std::vector<SpotLightData> spotLightData;

            m_scene.forEachLight([&](size_t, Light& light) {

                int shadowMapIndex = light.castsShadows() ? nextShadowMapIndex++ : -1;
                ShadowMapData shadowMapData { .textureIndex = shadowMapIndex };

                vec3 lightColor = light.color * light.intensityValue() * m_scene.lightPreExposureValue();

                switch (light.type()) {
                case Light::Type::DirectionalLight: {
                    dirLightData.emplace_back(DirectionalLightData { .shadowMap = shadowMapData,
                                                                     .color = lightColor,
                                                                     .exposure = m_scene.lightPreExposureValue(),
                                                                     .worldSpaceDirection = vec4(normalize(light.forwardDirection()), 0.0),
                                                                     .viewSpaceDirection = viewFromWorld * vec4(normalize(light.forwardDirection()), 0.0),
                                                                     .lightProjectionFromWorld = light.viewProjection(),
                                                                     .lightProjectionFromView = light.viewProjection() * worldFromView });
                    break;
                }
                case Light::Type::SpotLight: {
                    SpotLight& spotLight = static_cast<SpotLight&>(light);
                    spotLightData.emplace_back(SpotLightData { .shadowMap = shadowMapData,
                                                               .color = lightColor,
                                                               .exposure = m_scene.lightPreExposureValue(),
                                                               .worldSpaceDirection = vec4(normalize(light.forwardDirection()), 0.0f),
                                                               .viewSpaceDirection = viewFromWorld * vec4(normalize(light.forwardDirection()), 0.0f),
                                                               .lightProjectionFromWorld = light.viewProjection(),
                                                               .lightProjectionFromView = light.viewProjection() * worldFromView,
                                                               .worldSpacePosition = vec4(light.position(), 0.0f),
                                                               .viewSpacePosition = viewFromWorld * vec4(light.position(), 1.0f),
                                                               .outerConeHalfAngle = spotLight.outerConeAngle / 2.0f,
                                                               .iesProfileIndex = 0 /* todo: set correctly */,
                                                               ._pad0 = vec2() });
                    break;
                }
                case Light::Type::PointLight:
                default:
                    ASSERT_NOT_REACHED();
                    break;
                }
            });

            uploadBuffer.upload(dirLightData, dirLightDataBuffer);
            uploadBuffer.upload(spotLightData, spotLightDataBuffer);

            LightMetaData metaData { .numDirectionalLights = (int)dirLightData.size(),
                                     .numSpotLights = (int)spotLightData.size() };
            uploadBuffer.upload(metaData, lightMetaDataBuffer);

            std::vector<PerLightShadowData> shadowData;
            m_scene.forEachShadowCastingLight([&](size_t, Light& light) {
                shadowData.push_back({ .lightViewFromWorld = light.lightViewMatrix(),
                                       .lightProjectionFromWorld = light.viewProjection(),
                                       .constantBias = light.constantBias(),
                                       .slopeBias = light.slopeBias() });
            });
            uploadBuffer.upload(shadowData, lightShadowDataBuffer);
        }

        cmdList.executeBufferCopyOperations(uploadBuffer);
    };
}
