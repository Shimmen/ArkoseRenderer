#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

// NOTE: When we have multiple BRDFs we can just define permutations of this shader

#ifndef RT_EVALUATE_DIRECT_LIGHT
#define RT_EVALUATE_DIRECT_LIGHT 1
#endif

#include <common/brdf.glsl>
#include <common/iesProfile.glsl>
#include <common/lighting.glsl>
#include <common/namedUniforms.glsl>
#include <rayTracing/common/common.glsl>
#include <shared/CameraState.h>
#include <shared/SceneData.h>
#include <shared/LightData.h>

layout(location = 0) rayPayloadIn RayPayloadMain payload;
hitAttribute vec3 attribs;

layout(location = 1) rayPayload RayPayloadShadow shadowPayload;

layout(set = 0, binding = 0) uniform AccelerationStructure topLevelAS;
layout(set = 0, binding = 1) uniform CameraStateBlock { CameraState camera; };

layout(set = 1, binding = 0, scalar) buffer readonly TriangleMeshes { RTTriangleMesh meshes[]; };
layout(set = 1, binding = 1, scalar) buffer readonly Indices        { uint indices[]; };
layout(set = 1, binding = 2, scalar) buffer readonly Vertices       { Vertex vertices[]; };

layout(set = 2, binding = 0) buffer readonly MaterialBlock { ShaderMaterial materials[]; };
layout(set = 2, binding = 1) uniform sampler2D textures[];

layout(set = 3, binding = 0) uniform LightMetaDataBlock { LightMetaData lightMeta; };
layout(set = 3, binding = 1) buffer readonly DirLightDataBlock { DirectionalLightData directionalLights[]; };
layout(set = 3, binding = 2) buffer readonly SphereLightDataBlock { SphereLightData sphereLights[]; };
layout(set = 3, binding = 3) buffer readonly SpotLightDataBlock { SpotLightData spotLights[]; };

NAMED_UNIFORMS_STRUCT(RayTracingPushConstants, constants)

float traceShadowRay(vec3 X, vec3 L, float maxDistance)
{
    // NOTE: Yes, this means we treat all non-opaque geometry as opaque too. This is probably good enough for this use case.
    uint flags = RayFlags_TerminateOnFirstHit | RayFlags_SkipClosestHitShader | RayFlags_Opaque;
    uint cullMask = 0xff;

    const int shadowPayloadIdx = 1;

    // Assume we are in shadow, and if the shadow miss shader activates we are *not* in shadow
    shadowPayload.inShadow = true;

    traceRay(topLevelAS, flags, cullMask,
             0, // sbtRecordOffset
             0, // sbtRecordStride
             1, // missIndex
             X, 0.025, L, maxDistance,
             shadowPayloadIdx);

    return shadowPayload.inShadow ? 0.0 : 1.0;
}

vec3 evaluateDirectionalLight(DirectionalLightData light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 L = -normalize(light.worldSpaceDirection.xyz);
    float LdotN = dot(L, N);

    if (LdotN > 0.0) {

        vec3 hitPoint = rt_WorldRayOrigin + rt_RayHitT * rt_WorldRayDirection;
        float shadowFactor = traceShadowRay(hitPoint, L, 2.0 * camera.zFar);

        vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
        vec3 directLight = light.color * shadowFactor;

        return brdf * LdotN * directLight;
    }

    return vec3(0.0);
}

vec3 evaluateSphereLight(SphereLightData light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 hitPoint = rt_WorldRayOrigin + rt_RayHitT * rt_WorldRayDirection;
    vec3 toLight = light.worldSpacePosition.xyz - hitPoint;
    vec3 L = normalize(toLight);
    float LdotN = dot(L, N);

    if (LdotN > 0.0) {

        float distanceToLight = length(toLight);
        float shadowFactor = traceShadowRay(hitPoint, L, distanceToLight - 0.001);

        float distanceAttenuation = calculateLightDistanceAttenuation(distanceToLight, light.lightSourceRadius, light.lightRadius);

        vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
        vec3 directLight = light.color * shadowFactor * distanceAttenuation;

        return brdf * LdotN * directLight;
    }

    return vec3(0.0);
}

vec3 evaluateSpotLight(SpotLightData light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 L = -normalize(light.worldSpaceDirection.xyz);
    float LdotN = dot(L, N);

    if (LdotN > 0.0) {

        vec3 hitPoint = rt_WorldRayOrigin + rt_RayHitT * rt_WorldRayDirection;
        vec3 toLight = light.worldSpacePosition.xyz - hitPoint;
        float distanceToLight = length(toLight);

        vec3 normalizedToLight = toLight / distanceToLight;
        float shadowFactor = traceShadowRay(hitPoint, normalizedToLight, distanceToLight - 0.001);

        float distanceAttenuation = 1.0 / square(distanceToLight); // epsilon term??

        float cosConeAngle = dot(L, normalizedToLight);
        float iesValue = evaluateIESLookupTable(textures[nonuniformEXT(light.iesProfileIndex)], light.outerConeHalfAngle, cosConeAngle);

        vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
        vec3 directLight = light.color * shadowFactor * distanceAttenuation * iesValue;

        return brdf * LdotN * directLight;
    }

    return vec3(0.0);
}

void main()
{
    RTTriangleMesh mesh = meshes[rt_InstanceCustomIndex];
    ShaderMaterial material = materials[mesh.materialIndex];

    ivec3 idx = ivec3(indices[mesh.firstIndex + 3 * gl_PrimitiveID + 0],
                      indices[mesh.firstIndex + 3 * gl_PrimitiveID + 1],
                      indices[mesh.firstIndex + 3 * gl_PrimitiveID + 2]);

    Vertex v0 = vertices[mesh.firstVertex + idx.x];
    Vertex v1 = vertices[mesh.firstVertex + idx.y];
    Vertex v2 = vertices[mesh.firstVertex + idx.z];

    const vec3 b = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    vec3 N = normalize(v0.normal.xyz * b.x + v1.normal.xyz * b.y + v2.normal.xyz * b.z);
    // TODO: This is not 100% accurate due to the smooth interpolation. With GL_EXT_ray_tracing we can just check hit kind.
    bool backface = dot(N, normalize(rt_ObjectRayDirection)) > 1e-6;
    N = (backface) ? -N : N;

    // NOTE: Assumes uniform scaling!
    mat3 normalMatrix = mat3(rt_ObjectToWorld);
    N = normalize(normalMatrix * N);

    vec2 uv = v0.texCoord.xy * b.x + v1.texCoord.xy * b.y + v2.texCoord.xy * b.z;

    vec3 baseColor = texture(textures[nonuniformEXT(material.baseColor)], uv).rgb * material.colorTint.rgb;
    vec3 emissive = texture(textures[nonuniformEXT(material.emissive)], uv).rgb;

    vec4 metallicRoughness = texture(textures[nonuniformEXT(material.metallicRoughness)], uv);
    float metallic = metallicRoughness.b;
    float roughness = metallicRoughness.g;

    vec3 V = -rt_WorldRayDirection;

    #if RT_EVALUATE_DIRECT_LIGHT
    {
        vec3 ambient = constants.ambientAmount * baseColor;
        vec3 color = emissive + ambient;

        for (uint i = 0; i < lightMeta.numDirectionalLights; ++i) {
            color += evaluateDirectionalLight(directionalLights[i], V, N, baseColor, roughness, metallic);
        }

        for (uint i = 0; i < lightMeta.numSphereLights; ++i) {
            color += evaluateSphereLight(sphereLights[i], V, N, baseColor, roughness, metallic);
        }

        for (uint i = 0; i < lightMeta.numSpotLights; ++i) {
            color += evaluateSpotLight(spotLights[i], V, N, baseColor, roughness, metallic);
        }

        payload.color = color;
    }
    #else 
    {
        payload.color = baseColor;
    }
    #endif

    payload.hitT = (backface) ? -rt_RayHitT : rt_RayHitT;

    #if RT_USE_EXTENDED_RAY_PAYLOAD
        payload.baseColor = baseColor;
        payload.normal = N;
        payload.roughness = roughness;
        payload.metallic = metallic;
    #endif
}
