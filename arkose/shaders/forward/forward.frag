#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include <common/brdf.glsl>
#include <common/gBuffer.glsl>
#include <common/iesProfile.glsl>
#include <common/namedUniforms.glsl>
#include <shared/CameraState.h>
#include <shared/LightData.h>
#include <shared/SceneData.h>
#include <shared/ShaderBlendMode.h>

layout(location = 0) flat in int vMaterialIndex;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in vec3 vPosition;
layout(location = 3) in vec3 vNormal;
layout(location = 4) in vec3 vTangent;
layout(location = 5) in flat float vBitangentSign;
layout(location = 6) in vec4 vCurrFrameProjectedPos;
layout(location = 7) in vec4 vPrevFrameProjectedPos;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(set = 1, binding = 0) buffer readonly MaterialBlock { ShaderMaterial materials[]; };
layout(set = 1, binding = 1) uniform sampler2D textures[];

layout(set = 2, binding = 0) uniform LightMetaDataBlock { LightMetaData lightMeta; };
layout(set = 2, binding = 1) buffer readonly DirLightDataBlock { DirectionalLightData directionalLights[]; };
layout(set = 2, binding = 2) buffer readonly SpotLightDataBlock { SpotLightData spotLights[]; };

layout(set = 4, binding = 0) uniform sampler2D directionalLightProjectedShadowTex;
layout(set = 4, binding = 1) uniform sampler2D localLightShadowMapAtlasTex;
layout(set = 4, binding = 2) buffer readonly ShadowMapViewportBlock { vec4 localLightShadowMapViewports[]; };

NAMED_UNIFORMS(pushConstants,
    float ambientAmount;
    vec2 frustumJitterCorrection;
    vec2 invTargetSize;
)

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oNormalVelocity;
layout(location = 2) out vec4 oMaterialProps;
layout(location = 3) out vec4 oBaseColor;
layout(location = 4) out vec4 oDiffuseGI;

vec3 evaluateDirectionalLight(DirectionalLightData light, bool hasShadow, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 L = -normalize(light.viewSpaceDirection.xyz);

    vec2 sampleTexCoords = gl_FragCoord.xy * pushConstants.invTargetSize;
    float shadowFactor = hasShadow ? texture(directionalLightProjectedShadowTex, sampleTexCoords).r : 1.0;

    vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
    vec3 directLight = light.color * shadowFactor;

    float LdotN = max(dot(L, N), 0.0);
    return brdf * LdotN * directLight;
}

float evaluateLocalLightShadow(uint shadowIdx, mat4 lightProjectionFromView, vec3 viewSpacePos)
{
    vec4 shadowViewport = localLightShadowMapViewports[shadowIdx];

    // No shadow for this light
    if (lengthSquared(shadowViewport) < 1e-4) {
        return 1.0;
    }

    vec4 posInShadowMap = lightProjectionFromView * vec4(viewSpacePos, 1.0);
    posInShadowMap.xyz /= posInShadowMap.w;

    vec2 shadowMapUv = (posInShadowMap.xy * 0.5 + 0.5); // uv in the whole atlas
    shadowMapUv *= shadowViewport.zw; // scale to the appropriate viewport size
    shadowMapUv += shadowViewport.xy; // offset to the first pixel of the viewport

    float mapDepth = texture(localLightShadowMapAtlasTex, shadowMapUv).x;
    return (mapDepth < posInShadowMap.z) ? 0.0 : 1.0;
}

vec3 evaluateSpotLight(SpotLightData light, uint shadowIdx, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 L = -normalize(light.viewSpaceDirection.xyz);

    float shadowFactor = evaluateLocalLightShadow(shadowIdx, light.lightProjectionFromView, vPosition);

    vec3 toLight = light.viewSpacePosition.xyz - vPosition;
    float dist = length(toLight);
    float distanceAttenuation = 1.0 / square(dist);

    float cosConeAngle = dot(L, toLight / dist);
    float iesValue = evaluateIESLookupTable(textures[nonuniformEXT(light.iesProfileIndex)], light.outerConeHalfAngle, cosConeAngle);

    vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
    vec3 directLight = light.color * shadowFactor * distanceAttenuation * iesValue;

    float LdotN = max(dot(L, N), 0.0);
    return brdf * LdotN * directLight;
}

void main()
{
    ShaderMaterial material = materials[vMaterialIndex];

    vec4 inputBaseColor = texture(textures[nonuniformEXT(material.baseColor)], vTexCoord).rgba;

#if FORWARD_BLEND_MODE == BLEND_MODE_MASKED
    float mask = inputBaseColor.a;
    if (mask < material.maskCutoff) {
        discard;
    }
#endif

    vec3 baseColor = inputBaseColor.rgb * material.colorTint.rgb;
    vec3 emissive = texture(textures[nonuniformEXT(material.emissive)], vTexCoord).rgb;

    vec4 metallicRoughness = texture(textures[nonuniformEXT(material.metallicRoughness)], vTexCoord);
    float metallic = metallicRoughness.b;
    float roughness = metallicRoughness.g;

// NOTE: This is only really for debugging! In general we try to avoid permutations for very common cases (almost everything will be normal mapped in practice)
// (If we want to make normal mapping a proper permutation we would also want to exclude interpolats vTangent and vBitangentSign)
#define FORWARD_USE_NORMAL_MAPPING 1
// NOTE: We have to use 2-component normals when using BC5 compressed normal maps, but we always *can* use it, which is nice since we avoid permutations.
// In practice we will loose some level of precision by doing the reconstruction though, so the old path is left for A/B comparison purposes.
#define FORWARD_USE_2COMPONENT_NORMALS 1
#if FORWARD_USE_NORMAL_MAPPING
    vec3 packedNormal = texture(textures[nonuniformEXT(material.normalMap)], vTexCoord).rgb;
    #if FORWARD_USE_2COMPONENT_NORMALS
        vec3 tangentNormal = vec3(packedNormal.rg * 2.0 - 1.0, 0.0);
        tangentNormal.z = sqrt(clamp(1.0 - lengthSquared(tangentNormal.xy), 0.0, 1.0));
    #else
        vec3 tangentNormal = packedNormal * 2.0 - 1.0;
    #endif

    // Using MikkT space (http://www.mikktspace.com/)
    vec3 bitangent = vBitangentSign * cross(vNormal, vTangent);
    vec3 N = normalize(tangentNormal.x * vTangent + tangentNormal.y * bitangent + tangentNormal.z * vNormal);
#else
    vec3 N = vNormal;
#endif

    vec3 V = -normalize(vPosition);

    vec3 ambient = pushConstants.ambientAmount * baseColor;
    vec3 color = emissive + ambient;

    for (uint i = 0; i < lightMeta.numDirectionalLights; ++i) {

        // We only have shadow for the 0th directional light as they are pre-projected. If needed we could quite easily support up to 4 shadowed directional light
        // by storing the projected shadow in an RGBA texture with a projected shadow per channel. However, a single dir. shadow should almost always be enough.
        bool hasShadow = i == 0;

        color += evaluateDirectionalLight(directionalLights[i], hasShadow, V, N, baseColor, roughness, metallic);
    }

    // TODO: Use tiles or clusters to minimize number of light evaluations!
    {
        uint shadowIdx = 0;
        for (uint i = 0; i < lightMeta.numSpotLights; ++i) {
            color += evaluateSpotLight(spotLights[i], shadowIdx++, V, N, baseColor, roughness, metallic);
        }
    }

    vec2 velocity;
    {
        vec2 currentPos = vCurrFrameProjectedPos.xy / vCurrFrameProjectedPos.w;
        vec2 previousPos = vPrevFrameProjectedPos.xy / vPrevFrameProjectedPos.w;

        velocity = (currentPos - previousPos) * vec2(0.5, 0.5); // in uv-space
        velocity -= pushConstants.frustumJitterCorrection;

        //velocity = abs(velocity) * 100.0; // debug code
    }

    oColor = vec4(color, 1.0);
    oNormalVelocity = vec4(encodeNormal(N), velocity);
    oMaterialProps = vec4(roughness, metallic, 0.0, 0.0);
    oBaseColor = vec4(baseColor, 0.0);
}