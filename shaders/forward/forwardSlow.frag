#version 450
#extension GL_ARB_separate_shader_objects : enable

#include <shared/CameraState.h>
#include <shared/ForwardData.h>
#include <shared/LightData.h>
#include <common/brdf.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vPosition;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in mat3 vTbnMatrix;

layout(set = 0, binding = 0) uniform CameraStateBlock
{
    CameraState camera;
};

layout(set = 1, binding = 1) uniform sampler2D uBaseColor;
layout(set = 1, binding = 2) uniform sampler2D uNormalMap;
layout(set = 1, binding = 3) uniform sampler2D uMetallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D uEmissive;

layout(set = 2, binding = 0) uniform sampler2D uDirLightShadowMap;
layout(set = 2, binding = 1) uniform DirLightBlock
{
    DirectionalLight dirLight;
};

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oNormal;
layout(location = 2) out vec4 oBaseColor;

layout(push_constant) uniform PushConstants {
    bool writeColor;
    bool forceDiffuse;
    float ambientAmount;
};

// TODO: Move to some general location!

float evaluateShadow(sampler2D shadowMap, mat4 lightProjectionFromView, vec3 viewSpacePos)
{
    vec4 posInShadowMap = lightProjectionFromView * vec4(viewSpacePos, 1.0);
    posInShadowMap.xyz /= posInShadowMap.w;
    vec2 shadowMapUv = posInShadowMap.xy * 0.5 + 0.5;

    // TODO: Fix this! I think the reason we have to do this here is because we have texture repeat and linear filtering!
    const float eps = 0.01;
    if (shadowMapUv.x < eps || shadowMapUv.y < eps || shadowMapUv.x > 1.0-eps || shadowMapUv.y > 1.0-eps) {
        return 1.0;
    }

    float mapDepth = texture(shadowMap, shadowMapUv).x;

    // This isn't optimal but it works for now
    vec2 pixelSize = 1.0 / textureSize(shadowMap, 0);
    float bias = max(pixelSize.x, pixelSize.y) + 0.006;

    // (remember: 1 is furthest away, 0 is closest!)
    return (mapDepth < posInShadowMap.z - bias) ? 0.0 : 1.0;
}

vec3 evaluateDirectionalLight(DirectionalLight light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 lightColor = light.colorAndIntensity.a * light.colorAndIntensity.rgb;
    vec3 L = -normalize(light.viewSpaceDirection.xyz);

    mat4 lightProjectionFromView = light.lightProjectionFromWorld * camera.worldFromView;
    float shadowFactor = evaluateShadow(uDirLightShadowMap, lightProjectionFromView, vPosition);

    vec3 directLight = lightColor * shadowFactor;

    vec3 brdf;
    if (forceDiffuse) {
        brdf = baseColor * diffuseBRDF();
    } else {
        brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
    }

    float LdotN = max(dot(L, N), 0.0);
    return brdf * LdotN * directLight;
}

void main()
{
    vec4 inputBaseColor = texture(uBaseColor, vTexCoord).rgba;
    if (inputBaseColor.a < 1e-2) {
        discard;
    }

    vec3 baseColor = inputBaseColor.rgb;
    vec3 emissive = texture(uEmissive, vTexCoord).rgb;

    vec4 metallicRoughness = texture(uMetallicRoughnessMap, vTexCoord);
    float metallic = metallicRoughness.b;
    float roughness = metallicRoughness.g;

    vec3 packedNormal = texture(uNormalMap, vTexCoord).rgb;
    vec3 mappedNormal = normalize(packedNormal * 2.0 - 1.0);
    vec3 N = normalize(vTbnMatrix * mappedNormal);

    vec3 V = -normalize(vPosition);

    vec3 ambient = ambientAmount * (writeColor ? baseColor : vec3(1.0));
    vec3 color = emissive + ambient;

    // TODO: Evaluate ALL lights that will have an effect on this pixel/tile/cluster or whatever we go with
    color += evaluateDirectionalLight(dirLight, V, N, writeColor ? baseColor : vec3(1.0), roughness, metallic);

    oColor = vec4(color, 1.0);
    oNormal = vec4(N, 0.0);
    oBaseColor = vec4(baseColor, 0.0);
}
