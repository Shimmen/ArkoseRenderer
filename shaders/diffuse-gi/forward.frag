#version 460

#include <common/brdf.glsl>
#include <common/shadow.glsl>
#include <shared/CameraState.h>
#include <shared/ForwardData.h>
#include <shared/LightData.h>

layout(location = 0) in vec3 vPosition;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in vec3 vNormal;
layout(location = 3) flat in int vMaterialIndex;

layout(set = 0, binding = 0) uniform CameraBlock { CameraMatrices cameras[6]; };

layout(set = 1, binding = 1) uniform MaterialBlock { ForwardMaterial materials[FORWARD_MAX_MATERIALS]; };
layout(set = 1, binding = 2) uniform sampler2D textures[FORWARD_MAX_TEXTURES];

layout(set = 2, binding = 0) uniform sampler2D dirLightShadowMapTex;
layout(set = 2, binding = 1) uniform LightDataBlock { DirectionalLightData dirLight; };

layout(push_constant) uniform PushConstants {
    uint sideIndex;
    float ambientLx;
};

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oDistance;

vec3 evaluateDirectionalLight(DirectionalLightData light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 lightColor = light.colorAndIntensity.a * light.colorAndIntensity.rgb;
    vec3 L = -normalize(light.viewSpaceDirection.xyz);

    mat4 lightProjectionFromView = light.lightProjectionFromWorld * cameras[sideIndex].worldFromView;
    float shadowFactor = evaluateShadow(dirLightShadowMapTex, lightProjectionFromView, vPosition);

    vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
    vec3 directLight = lightColor * shadowFactor;

    float LdotN = max(dot(L, N), 0.0);
    return brdf * LdotN * directLight;
}

void main()
{
    ForwardMaterial material = materials[vMaterialIndex];

    vec4 inputBaseColor = texture(textures[material.baseColor], vTexCoord).rgba;
    if (inputBaseColor.a < 1e-2) {
        discard;
        return;
    }

    vec3 baseColor = inputBaseColor.rgb;
    vec3 emissive = texture(textures[material.emissive], vTexCoord).rgb;

    vec4 metallicRoughness = texture(textures[material.metallicRoughness], vTexCoord);
    float metallic = metallicRoughness.b;
    float roughness = metallicRoughness.g;

    vec3 N = normalize(vNormal);
    vec3 V = -normalize(vPosition);

    vec3 ambient = ambientLx * baseColor;
    vec3 color = emissive + ambient;

    // TODO: Evaluate ALL lights that will have an effect on this pixel/tile/cluster or whatever we go with
    color += evaluateDirectionalLight(dirLight, V, N, baseColor, roughness, metallic);

    oColor = vec4(color, 1.0);

    float distance = length(vPosition);
    oDistance = vec4(distance, square(distance), 0.0, 0.0);
}
