#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include <common/brdf.glsl>
#include <common/lighting.glsl>
#include <common/material.glsl>
#include <common/namedUniforms.glsl>
#include <common/random.glsl>
#include <pathtracer/common.glsl>
#include <pathtracer/material.glsl>
#include <shared/CameraState.h>
#include <shared/SceneData.h>
#include <shared/LightData.h>

layout(set = 0, binding = 0) uniform AccelerationStructure topLevelAS;

layout(location = 0) rayPayloadIn PathTracerRayPayload payload;
hitAttribute vec3 attribs;

layout(location = 1) rayPayload PathTracerShadowRayPayload shadowPayload;

layout(set = 0, binding = 1) uniform CameraStateBlock { CameraState camera; };

DeclareCommonBindingSet_RTMesh(1)
DeclareCommonBindingSet_Material(2)
DeclareCommonBindingSet_Light(3)

NAMED_UNIFORMS_STRUCT(PathTracerPushConstants, constants)

float traceShadowRay(vec3 X, vec3 L, float maxDistance)
{
    uint flags = RayFlags_SkipClosestHitShader | RayFlags_Opaque;
    uint cullMask = RT_HIT_MASK_OPAQUE | RT_HIT_MASK_MASKED; // todo: support translucent shadowing!

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
        float iesValue = evaluateIESLookupTable(material_getTexture(light.iesProfileIndex), light.outerConeHalfAngle, cosConeAngle);

        vec3 brdf = evaluateBRDF(L, V, N, baseColor, roughness, metallic);
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
    vec2 uv = v0.texCoord.xy * b.x + v1.texCoord.xy * b.y + v2.texCoord.xy * b.z;

    vec3 normal = normalize(v0.normal.xyz * b.x + v1.normal.xyz * b.y + v2.normal.xyz * b.z);
    // TODO: This is not 100% accurate due to the smooth interpolation. With GL_EXT_ray_tracing we can just check hit kind.
    bool backface = dot(normal, normalize(rt_ObjectRayDirection)) > 1e-6;
    normal = (backface) ? -normal : normal;

#if 1
    vec2 packedNormal = texture(material_getTexture(material.normalMap), uv).rg;
    vec3 tangentNormal = vec3(packedNormal.rg * 2.0 - 1.0, 0.0);
    tangentNormal.z = sqrt(clamp(1.0 - lengthSquared(tangentNormal.xy), 0.0, 1.0));

    // Using MikkT space (http://www.mikktspace.com/)
    vec3 tangent = normalize(v0.tangent.xyz * b.x + v1.tangent.xyz * b.y + v2.tangent.xyz * b.z);
    vec3 bitangent = v0.tangent.w * cross(normal, tangent);
    normal = normalize(tangentNormal.x * tangent + tangentNormal.y * bitangent + tangentNormal.z * normal);
#endif

    // NOTE: Assumes uniform scaling!
    mat3 normalMatrix = mat3(rt_ObjectToWorld);
    vec3 N = normalize(normalMatrix * normal);

    vec3 baseColor = texture(material_getTexture(material.baseColor), uv).rgb * material.colorTint.rgb;
    vec3 emissive = texture(material_getTexture(material.emissive), uv).rgb * material.emissiveFactor;

    vec4 metallicRoughness = texture(material_getTexture(material.metallicRoughness), uv);
    float metallic = metallicRoughness.b * material.metallicFactor;
    float roughness = metallicRoughness.g * material.roughnessFactor;

    vec3 radiance = emissive;

    // Direct light (analytic lights) (in world space)
    {
        vec3 V = -rt_WorldRayDirection;

        for (uint i = 0; i < light_getDirectionalLightCount(); ++i) {
            radiance += evaluateDirectionalLight(light_getDirectionalLight(i), V, N, baseColor, roughness, metallic);
        }

        for (uint i = 0; i < light_getSphereLightCount(); ++i) {
            radiance += evaluateSphereLight(light_getSphereLight(i), V, N, baseColor, roughness, metallic);
        }

        for (uint i = 0; i < light_getSpotLightCount(); ++i) {
            radiance += evaluateSpotLight(light_getSpotLight(i), V, N, baseColor, roughness, metallic);
        }
    }

    payload.radiance += payload.attenuation * radiance;

    // Scatter
    {
        PathTraceMaterial material;
        material.baseColor = baseColor;
        material.roughness = max(0.001, roughness); // hack! it should be able to work with zero-roughness materials!
        material.metallic = metallic;

        vec3 B1, B2;
        createOrthonormalBasis(N, B1, B2);
        mat3 tangentBasisMatrix = mat3(B1, B2, N);
        mat3 inverseTangentBasisMatrix = transpose(tangentBasisMatrix); // rotation only

        // In tangent space!
        vec3 V = inverseTangentBasisMatrix * -rt_WorldRayDirection;

        vec3 L;
        float pdf;
        vec3 brdf = sampleOpaqueMicrofacetMaterial(payload, material, V, L, pdf);

        if (pdf > 0.0) {
            payload.attenuation *= brdf / pdf;
        } else {
            payload.attenuation = vec3(0.0); // (kill the path)
            return;
        }

        // Transform back to world space
        vec3 L_world = tangentBasisMatrix * L;

        payload.scatteredDirection = L_world;
    }

    // TODO: Encode backface hits more explicitly than this. For now we're not even handling them anyway..
    payload.hitT = (backface) ? -rt_RayHitT : rt_RayHitT;
}