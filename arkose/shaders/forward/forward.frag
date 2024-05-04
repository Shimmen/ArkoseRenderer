#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include <common/brdf.glsl>
#include <common/gBuffer.glsl>
#include <common/lighting.glsl>
#include <common/material.glsl>
#include <common/namedUniforms.glsl>
#include <forward/forwardCommon.glsl>
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

DeclareCommonBindingSet_Material(3)
DeclareCommonBindingSet_Light(4)

layout(set = 5, binding = 0) uniform sampler2D directionalLightProjectedShadowTex;
layout(set = 5, binding = 1) uniform sampler2D sphereLightProjectedShadowTex;
layout(set = 5, binding = 2) uniform sampler2D localLightShadowMapAtlasTex;
layout(set = 5, binding = 3) buffer readonly ShadowMapViewportBlock { vec4 localLightShadowMapViewports[]; };

NAMED_UNIFORMS_STRUCT(ForwardPassConstants, constants)

layout(location = 0) out vec4 oColor;
#if FORWARD_BLEND_MODE != BLEND_MODE_TRANSLUCENT
layout(location = 1) out vec4 oNormalVelocity;
layout(location = 2) out vec4 oMaterialProps;
layout(location = 3) out vec4 oBaseColor;
#endif

vec3 evaluateDirectionalLight(DirectionalLightData light, bool hasShadow, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 L = -normalize(light.viewSpaceDirection.xyz);

    vec2 sampleTexCoords = gl_FragCoord.xy * constants.invTargetSize;
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

vec3 evaluateSphereLight(SphereLightData light, bool hasShadow, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    // TODO: Support multiple sphere lights with shadows!
    vec2 sampleTexCoords = gl_FragCoord.xy * constants.invTargetSize;
    float shadowFactor = hasShadow ? texture(sphereLightProjectedShadowTex, sampleTexCoords).r : 1.0;

    vec3 toLight = light.viewSpacePosition.xyz - vPosition;
    vec3 L = normalize(toLight);

    // If the light source is behind the geometric normal of the surface consider it in shadow,
    // even if a normal map could make the surface seem to be able to pick up light from the light.
    if (dot(normalize(vNormal), L) < 0.0) {
        shadowFactor = 0.0;
    }

    float dist = length(toLight);
    float distanceAttenuation = calculateLightDistanceAttenuation(dist, light.lightSourceRadius, light.lightRadius);

    vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
    vec3 directLight = light.color * shadowFactor * distanceAttenuation;

    float LdotN = max(dot(L, N), 0.0);
    return brdf * LdotN * directLight;
}

vec3 evaluateSpotLight(SpotLightData light, uint shadowIdx, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 L = -normalize(light.viewSpaceDirection.xyz);

    float shadowFactor = evaluateLocalLightShadow(shadowIdx, light.lightProjectionFromView, vPosition);

    vec3 toLight = light.viewSpacePosition.xyz - vPosition;
    float dist = length(toLight);
    float distanceAttenuation = 1.0 / square(dist);

    float cosConeAngle = dot(L, toLight / dist);
    float iesValue = evaluateIESLookupTable(material_getTexture(light.iesProfileIndex), light.outerConeHalfAngle, cosConeAngle);

    vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
    vec3 directLight = light.color * shadowFactor * distanceAttenuation * iesValue;

    float LdotN = max(dot(L, N), 0.0);
    return brdf * LdotN * directLight;
}

void main()
{
    ShaderMaterial material = material_getMaterial(vMaterialIndex);

    vec4 inputBaseColor = texture(material_getTexture(material.baseColor), vTexCoord, constants.mipBias).rgba;

#if FORWARD_BLEND_MODE == BLEND_MODE_MASKED
    float mask = inputBaseColor.a;
    if (mask < material.maskCutoff) {
        discard;
    }
#endif

    vec3 baseColor = inputBaseColor.rgb * material.colorTint.rgb;
    if (!constants.withMaterialColor) {
        baseColor = vec3(1.0);
    }

    vec3 emissive = texture(material_getTexture(material.emissive), vTexCoord, constants.mipBias).rgb;
    emissive *= material.emissiveFactor;

    vec4 metallicRoughness = texture(material_getTexture(material.metallicRoughness), vTexCoord, constants.mipBias);
    float metallic = metallicRoughness.b * material.metallicFactor;
    float roughness = metallicRoughness.g * material.roughnessFactor;

    vec3 V = -normalize(vPosition);

    vec3 normal = vNormal;
#if FORWARD_DOUBLE_SIDED
    if (dot(V, normal) < 0.0) {
        normal = -normal;
    }
#endif

// NOTE: This is only really for debugging! In general we try to avoid permutations for very common cases (almost everything will be normal mapped in practice)
// (If we want to make normal mapping a proper permutation we would also want to exclude interpolats vTangent and vBitangentSign)
#define FORWARD_USE_NORMAL_MAPPING 1
// NOTE: We have to use 2-component normals when using BC5 compressed normal maps, but we always *can* use it, which is nice since we avoid permutations.
// In practice we will loose some level of precision by doing the reconstruction though, so the old path is left for A/B comparison purposes.
#define FORWARD_USE_2COMPONENT_NORMALS 1
#if FORWARD_USE_NORMAL_MAPPING
    vec3 packedNormal = texture(material_getTexture(material.normalMap), vTexCoord, constants.mipBias).rgb;
    #if FORWARD_USE_2COMPONENT_NORMALS
        vec3 tangentNormal = vec3(packedNormal.rg * 2.0 - 1.0, 0.0);
        tangentNormal.z = sqrt(clamp(1.0 - lengthSquared(tangentNormal.xy), 0.0, 1.0));
    #else
        vec3 tangentNormal = packedNormal * 2.0 - 1.0;
    #endif

    // Using MikkT space (http://www.mikktspace.com/)
    vec3 bitangent = vBitangentSign * cross(normal, vTangent);
    vec3 N = normalize(tangentNormal.x * vTangent + tangentNormal.y * bitangent + tangentNormal.z * normal);
#else
    vec3 N = normal;
#endif

    vec3 ambient = constants.ambientAmount * baseColor;
    vec3 color = emissive + ambient;

    for (uint i = 0; i < light_getDirectionalLightCount(); ++i) {

#if FORWARD_BLEND_MODE == BLEND_MODE_TRANSLUCENT
        // NOTE: Since the shadow is pre-projected we can't use it for geometry that doesn't write to the depth buffer in the prepass
        // TODO: Move to using only ray traced translucency, so we don't have to worry about these cases.
        bool hasShadow = false;
#else
        // We only have shadow for the 0th directional light as they are pre-projected. If needed we could quite easily support up to 4 shadowed directional light
        // by storing the projected shadow in an RGBA texture with a projected shadow per channel. However, a single dir. shadow should almost always be enough.
        bool hasShadow = i == 0;
#endif

        color += evaluateDirectionalLight(light_getDirectionalLight(i), hasShadow, V, N, baseColor, roughness, metallic);
    }

    // TODO: Use tiles or clusters to minimize number of light evaluations!
    {
        for (uint i = 0; i < light_getSphereLightCount(); ++i) {
#if FORWARD_BLEND_MODE == BLEND_MODE_TRANSLUCENT
            // NOTE: Since the shadow is pre-projected we can't use it for geometry that doesn't write to the depth buffer in the prepass
            // TODO: Move to using only ray traced translucency, so we don't have to worry about these cases.
            bool hasShadow = false;
#else
            bool hasShadow = i == 0; // todo: support multple shadowed point lights!
#endif
            color += evaluateSphereLight(light_getSphereLight(i), hasShadow, V, N, baseColor, roughness, metallic);
        }

        uint shadowIdx = 0;
        for (uint i = 0; i < light_getSpotLightCount(); ++i) {
            color += evaluateSpotLight(light_getSpotLight(i), shadowIdx++, V, N, baseColor, roughness, metallic);
        }
    }

    vec2 velocity;
    {
        vec2 currentPos = vCurrFrameProjectedPos.xy / vCurrFrameProjectedPos.w;
        vec2 previousPos = vPrevFrameProjectedPos.xy / vPrevFrameProjectedPos.w;

        velocity = (currentPos - previousPos) * vec2(0.5, 0.5); // in uv-space
        velocity -= constants.frustumJitterCorrection;

        //velocity = abs(velocity) * 100.0; // debug code
    }

#if FORWARD_BLEND_MODE != BLEND_MODE_TRANSLUCENT
    oColor = vec4(color, 1.0);
    oNormalVelocity = vec4(encodeNormal(N), velocity);
    oMaterialProps = vec4(roughness, metallic, 0.0, 0.0);
    oBaseColor = vec4(baseColor, 0.0);
#else
    float alpha = inputBaseColor.a * material.colorTint.a;
    color = max(color, vec3(0.01)); // HACK: Until we have a proper BxDF for glass
    oColor = vec4(color, alpha);
#endif
}
