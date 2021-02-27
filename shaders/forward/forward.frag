#version 460

#include <common/brdf.glsl>
#include <common/namedUniforms.glsl>
#include <common/shadow.glsl>
#include <shared/CameraState.h>
#include <shared/SceneData.h>
#include <shared/LightData.h>

// TODO: Define this when compiling the shader instead of here!
#define FORWARD_INCLUDE_INDIRECT_LIGHT 1

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vPosition;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in mat3 vTbnMatrix;
layout(location = 6) flat in int vMaterialIndex;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(set = 1, binding = 1) uniform MaterialBlock { ShaderMaterial materials[SCENE_MAX_MATERIALS]; };
layout(set = 1, binding = 2) uniform sampler2D textures[SCENE_MAX_TEXTURES];

layout(set = 2, binding = 0) uniform LightMetaDataBlock { LightMetaData lightMeta; };
layout(set = 2, binding = 1) buffer readonly DirLightDataBlock { DirectionalLightData directionalLights[]; };
layout(set = 2, binding = 2) buffer readonly SpotLightDataBlock { SpotLightData spotLights[]; };
layout(set = 2, binding = 3) uniform sampler2D shadowMaps[SCENE_MAX_SHADOW_MAPS];

#if FORWARD_INCLUDE_INDIRECT_LIGHT
#include <shared/ProbeGridData.h>
#include <diffuse-gi/probeSampling.glsl>
layout(set = 3, binding = 0) uniform ProbeGridDataBlock { ProbeGridData probeGridData; };
layout(set = 3, binding = 1) uniform sampler2DArray probeIrradianceTex;
layout(set = 3, binding = 2) uniform sampler2DArray probeDistanceTex;
#endif

NAMED_UNIFORMS(pushConstants,
    float ambientAmount;
)

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oNormal;
layout(location = 2) out vec4 oBaseColor;

vec3 evaluateDirectionalLight(DirectionalLightData light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 lightColor = light.colorAndIntensity.a * light.colorAndIntensity.rgb;
    vec3 L = -normalize(light.viewSpaceDirection.xyz);

    float shadowFactor = evaluateShadow(shadowMaps[light.shadowMap.textureIndex], light.lightProjectionFromView, vPosition);

    vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
    vec3 directLight = lightColor * shadowFactor;

    float LdotN = max(dot(L, N), 0.0);
    return brdf * LdotN * directLight;
}

#if FORWARD_INCLUDE_INDIRECT_LIGHT
vec3 evaluateIndirectLight(vec3 P, vec3 V, vec3 N, vec3 baseColor, float metallic, float roughness)
{
    vec3 worldSpacePos = vec3(camera.worldFromView * vec4(P, 1.0));
    vec3 worldSpaceNormal = normalize(mat3(camera.worldFromView) * N);

    // Assume glossy indirect light comes from the reflected direction L
    //vec3 L = reflect(-V, N); TODO!
    vec3 indirectGlossy = vec3(0.0);

    // TODO: Use physically plausible amounts! For now we just use a silly estimate for F since we don't actually include glossy stuff at the moment.
    float a = square(roughness);
    float fakeF = pow(a, 5.0);

    vec3 irradiance = computePrefilteredIrradiance(worldSpacePos, worldSpaceNormal, probeGridData, probeIrradianceTex, probeDistanceTex);
    vec3 indirectDiffuse = vec3(1.0 - metallic) * vec3(1.0 - fakeF) * baseColor * irradiance;

    return indirectDiffuse + indirectGlossy;
}
#endif

void main()
{
    ShaderMaterial material = materials[vMaterialIndex];

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

    vec3 packedNormal = texture(textures[material.normalMap], vTexCoord).rgb;
    vec3 mappedNormal = normalize(packedNormal * 2.0 - 1.0);
    vec3 N = normalize(vTbnMatrix * mappedNormal);

    vec3 V = -normalize(vPosition);

    vec3 ambient = pushConstants.ambientAmount * baseColor;
    vec3 color = emissive + ambient;

    // TODO: Use tiles or clusters to minimize number of light evaluations!
    {
        for (uint i = 0; i < lightMeta.numDirectionalLights; ++i) {
            color += evaluateDirectionalLight(directionalLights[i], V, N, baseColor, roughness, metallic);
        }

        for (uint i = 0; i < lightMeta.numSpotLights; ++i) {
            //color += evaluateSpotLight(spotLights[i], V, N, baseColor, roughness, metallic);
        }
    }

#if FORWARD_INCLUDE_INDIRECT_LIGHT
    color += evaluateIndirectLight(vPosition, V, N, baseColor, metallic, roughness);
#endif

    oColor = vec4(color, 1.0);
    oNormal = vec4(N, 0.0);
    oBaseColor = vec4(baseColor, 0.0);
}
