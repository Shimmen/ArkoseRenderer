#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

// NOTE: When we have multiple BRDFs we can just define permutations of this shader

#ifndef RT_EVALUATE_DIRECT_LIGHT
#define RT_EVALUATE_DIRECT_LIGHT 1
#endif

#include <common/brdf.glsl>
#include <common/lighting.glsl>
#include <common/material.glsl>
#include <common/namedUniforms.glsl>
#include <common/rayTracing.glsl>
#include <rayTracing/common/common.glsl>
#include <shared/CameraState.h>
#include <shared/SceneData.h>
#include <shared/LightData.h>

layout(location = 0) rayPayloadIn RayPayloadMain payload;
hitAttribute vec3 attribs;

layout(location = 1) rayPayload RayPayloadShadow shadowPayload;

layout(set = 0, binding = 0) uniform AccelerationStructure topLevelAS;
layout(set = 0, binding = 1) uniform CameraStateBlock { CameraState camera; };

DeclareCommonBindingSet_RTMesh(1)
DeclareCommonBindingSet_Material(2)
DeclareCommonBindingSet_Light(3)

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

vec3 evaluateDirectionalLight(DirectionalLightData light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic, float clearcoat, float clearcoatRoughness)
{
    vec3 L = -normalize(light.worldSpaceDirection.xyz);
    float LdotN = dot(L, N);

    if (LdotN > 0.0) {

        vec3 hitPoint = rt_WorldRayOrigin + rt_RayHitT * rt_WorldRayDirection;
        float shadowFactor = traceShadowRay(hitPoint, L, 2.0 * camera.zFar);

        vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic, clearcoat, clearcoatRoughness);
        vec3 directLight = light.color * shadowFactor;

        return brdf * LdotN * directLight;
    }

    return vec3(0.0);
}

vec3 evaluateSphereLight(SphereLightData light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic, float clearcoat, float clearcoatRoughness)
{
    vec3 hitPoint = rt_WorldRayOrigin + rt_RayHitT * rt_WorldRayDirection;
    vec3 toLight = light.worldSpacePosition.xyz - hitPoint;
    vec3 L = normalize(toLight);
    float LdotN = dot(L, N);

    if (LdotN > 0.0) {

        float distanceToLight = length(toLight);
        float shadowFactor = traceShadowRay(hitPoint, L, distanceToLight - 0.001);

        float distanceAttenuation = calculateLightDistanceAttenuation(distanceToLight, light.lightSourceRadius, light.lightRadius);

        vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic, clearcoat, clearcoatRoughness);
        vec3 directLight = light.color * shadowFactor * distanceAttenuation;

        return brdf * LdotN * directLight;
    }

    return vec3(0.0);
}

vec3 evaluateSpotLight(SpotLightData light, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic, float clearcoat, float clearcoatRoughness)
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

        mat3 lightViewMatrix = mat3(light.worldSpaceRight.xyz,
                                    light.worldSpaceUp.xyz,
                                    light.worldSpaceDirection.xyz);
        float iesValue = evaluateIESLookupTable(material_getTexture(light.iesProfileIndex), light.outerConeHalfAngle, lightViewMatrix, -normalizedToLight);

        vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic, clearcoat, clearcoatRoughness);
        vec3 directLight = light.color * shadowFactor * distanceAttenuation * iesValue;

        return brdf * LdotN * directLight;
    }

    return vec3(0.0);
}

void main()
{
    RTTriangleMesh mesh = rtmesh_getMesh(rt_InstanceCustomIndex);
    ShaderMaterial material = material_getMaterial(mesh.materialIndex);

    ivec3 idx = ivec3(rtmesh_getIndex(mesh.firstIndex + 3 * gl_PrimitiveID + 0),
                      rtmesh_getIndex(mesh.firstIndex + 3 * gl_PrimitiveID + 1),
                      rtmesh_getIndex(mesh.firstIndex + 3 * gl_PrimitiveID + 2));

    RTVertex v0 = rtmesh_getVertex(mesh.firstVertex + idx.x);
    RTVertex v1 = rtmesh_getVertex(mesh.firstVertex + idx.y);
    RTVertex v2 = rtmesh_getVertex(mesh.firstVertex + idx.z);

    const vec3 b = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    vec3 N = normalize(v0.normal.xyz * b.x + v1.normal.xyz * b.y + v2.normal.xyz * b.z);
#if defined(KHR_RAY_TRACING_GLSL)
    bool backface = rt_HitKind == rt_HitKindBackFace;
#else
    // TODO: This is not 100% accurate due to the smooth interpolation, but hit kind is not available in the nvidia extension.
    bool backface = dot(N, normalize(rt_ObjectRayDirection)) > 1e-6;
#endif
    N = (backface) ? -N : N;

    // NOTE: Assumes uniform scaling!
    mat3 normalMatrix = mat3(rt_ObjectToWorld);
    N = normalize(normalMatrix * N);

    vec2 uv = v0.texCoord.xy * b.x + v1.texCoord.xy * b.y + v2.texCoord.xy * b.z;

    vec3 baseColor = texture(material_getTexture(material.baseColor), uv).rgb * material.colorTint.rgb;
    vec3 emissive = texture(material_getTexture(material.emissive), uv).rgb * material.emissiveFactor;

    vec4 metallicRoughness = texture(material_getTexture(material.metallicRoughness), uv);
    float metallic = metallicRoughness.b * material.metallicFactor;
    float roughness = metallicRoughness.g * material.roughnessFactor;

    float clearcoat = material.clearcoat;
    float clearcoatRoughness = material.clearcoatRoughness;

    vec3 V = -rt_WorldRayDirection;

    #if RT_EVALUATE_DIRECT_LIGHT
    {
        vec3 ambient = constants.ambientAmount * baseColor;
        vec3 color = emissive + ambient;

        if (light_hasDirectionalLight()) {
            color += evaluateDirectionalLight(light_getDirectionalLight(), V, N, baseColor, roughness, metallic, clearcoat, clearcoatRoughness);
        }

        for (uint i = 0; i < light_getSphereLightCount(); ++i) {
            color += evaluateSphereLight(light_getSphereLight(i), V, N, baseColor, roughness, metallic, clearcoat, clearcoatRoughness);
        }

        for (uint i = 0; i < light_getSpotLightCount(); ++i) {
            color += evaluateSpotLight(light_getSpotLight(i), V, N, baseColor, roughness, metallic, clearcoat, clearcoatRoughness);
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
