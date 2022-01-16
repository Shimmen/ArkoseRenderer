#version 460

#include <common/brdf.glsl>
#include <common/namedUniforms.glsl>
#include <common/iesProfile.glsl>
#include <common/shadow.glsl>
#include <shared/BlendMode.h>
#include <shared/CameraState.h>
#include <shared/SceneData.h>
#include <shared/LightData.h>

layout(location = 0) flat in int vMaterialIndex;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in vec3 vPosition;
layout(location = 3) in vec4 vCurrFrameProjectedPos;
layout(location = 4) in vec4 vPrevFrameProjectedPos;
layout(location = 5 /*, 6, 7*/) in mat3 vTbnMatrix;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(set = 1, binding = 0) buffer readonly MaterialBlock { ShaderMaterial materials[]; };
layout(set = 1, binding = 1) uniform sampler2D textures[SCENE_MAX_TEXTURES];

layout(set = 2, binding = 0) uniform LightMetaDataBlock { LightMetaData lightMeta; };
layout(set = 2, binding = 1) buffer readonly DirLightDataBlock { DirectionalLightData directionalLights[]; };
layout(set = 2, binding = 2) buffer readonly SpotLightDataBlock { SpotLightData spotLights[]; };
layout(set = 2, binding = 3) uniform sampler2D shadowMaps[SCENE_MAX_SHADOW_MAPS];
layout(set = 2, binding = 4) uniform sampler2D iesLUTs[SCENE_MAX_IES_LUT];

#if FORWARD_INCLUDE_DDGI
#include <shared/DDGIData.h>
#include <ddgi/probeSampling.glsl>
layout(set = 5, binding = 0) uniform DDGIGridDataBlock { DDGIProbeGridData ddgiProbeGridData; };
layout(set = 5, binding = 1) uniform sampler2D ddgiIrradianceAtlas;
layout(set = 5, binding = 2) uniform sampler2D ddgiVisibilityAtlas;
#endif

NAMED_UNIFORMS(pushConstants,
    float ambientAmount;
    float indirectExposure;
    vec2 totalFrustumJitter;
)

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oNormal;
layout(location = 2) out vec4 oVelocity;
layout(location = 3) out vec4 oBaseColor;
layout(location = 4) out vec4 oDiffuseGI;

vec3 evaluateDirectionalLight(DirectionalLightData light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 L = -normalize(light.viewSpaceDirection.xyz);

    float shadowFactor = evaluateDirectionalShadow(shadowMaps[light.shadowMap.textureIndex], light.lightProjectionFromView, vPosition);

    vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
    vec3 directLight = light.color * shadowFactor;

    float LdotN = max(dot(L, N), 0.0);
    return brdf * LdotN * directLight;
}

vec3 evaluateSpotLight(SpotLightData light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 L = -normalize(light.viewSpaceDirection.xyz);

    float shadowFactor = evaluateShadow(shadowMaps[light.shadowMap.textureIndex], light.lightProjectionFromView, vPosition);

    vec3 toLight = light.viewSpacePosition.xyz - vPosition;
    float dist = length(toLight);
    float distanceAttenuation = 1.0 / square(dist);

    float cosConeAngle = dot(L, toLight / dist);
    float iesValue = evaluateIESLookupTable(iesLUTs[light.iesProfileIndex], light.outerConeHalfAngle, cosConeAngle);

    vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
    vec3 directLight = light.color * shadowFactor * distanceAttenuation * iesValue;

    float LdotN = max(dot(L, N), 0.0);
    return brdf * LdotN * directLight;
}

#if FORWARD_INCLUDE_DDGI
vec3 evaluateDDGIIndirectLight(vec3 P, vec3 V, vec3 N, vec3 baseColor, float metallic, float roughness)
{
    vec3 worldSpacePos = vec3(camera.worldFromView * vec4(P, 1.0));
    vec3 worldSpaceNormal = normalize(mat3(camera.worldFromView) * N);

    // Assume glossy indirect light comes from the reflected direction L
    //vec3 L = reflect(-V, N); TODO!
    vec3 indirectGlossy = vec3(0.0);

    // TODO: Use physically plausible amounts! For now we just use a silly estimate for F since we don't actually include glossy stuff at the moment.
    float a = square(roughness);
    float fakeF = pow(a, 5.0);

    vec3 irradiance = sampleDynamicDiffuseGlobalIllumination(worldSpacePos, worldSpaceNormal, ddgiProbeGridData, ddgiIrradianceAtlas, ddgiVisibilityAtlas);
    vec3 indirectDiffuse = vec3(1.0 - metallic) * vec3(1.0 - fakeF) * irradiance;

    return indirectDiffuse + indirectGlossy;
}
#endif

void main()
{
    ShaderMaterial material = materials[vMaterialIndex];

    vec4 inputBaseColor = texture(textures[material.baseColor], vTexCoord).rgba;

#if FORWARD_BLEND_MODE == BLEND_MODE_MASKED
    float mask = inputBaseColor.a;
    if (mask < material.maskCutoff) {
        discard;
    }
#endif

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
            color += evaluateSpotLight(spotLights[i], V, N, baseColor, roughness, metallic);
        }
    }

#if FORWARD_INCLUDE_DDGI
    vec3 ddgi = evaluateDDGIIndirectLight(vPosition, V, N, baseColor, metallic, roughness);
    oDiffuseGI = vec4(ddgi, 0.0);
#endif

    vec2 velocity;
    {
        vec2 currentPos = vCurrFrameProjectedPos.xy / vCurrFrameProjectedPos.w;
        vec2 previousPos = vPrevFrameProjectedPos.xy / vPrevFrameProjectedPos.w;

        velocity = (currentPos - previousPos) * vec2(0.5, 0.5); // in uv-space
        velocity -= pushConstants.totalFrustumJitter;

        //velocity = abs(velocity) * 100.0; // debug code
    }

    // TODO: Maybe use octahedral for normals and pack normal & velocity together?
    oColor = vec4(color, 1.0);
    oNormal = vec4(N, 0.0);
    oVelocity = vec4(velocity, 0.0, 0.0);
    oBaseColor = vec4(baseColor, 0.0);
}
