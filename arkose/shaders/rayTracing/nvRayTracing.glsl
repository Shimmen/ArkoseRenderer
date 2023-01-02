#ifndef RTX_RAY_TRACING_GLSL
#define RTX_RAY_TRACING_GLSL

// https://github.com/KhronosGroup/GLSL/blob/master/extensions/nv/GLSL_NV_ray_tracing.txt

#extension GL_NV_ray_tracing : require

// Common types

#define AccelerationStructure accelerationStructureNV

// Common type annotations

#define rayPayload rayPayloadNV
#define rayPayloadIn rayPayloadInNV
#define hitAttribute hitAttributeNV

// Ray flags

#define RayFlags_None gl_RayFlagsNoneNV
#define RayFlags_Opaque gl_RayFlagsOpaqueNV
#define RayFlags_NoOpaque gl_RayFlagsNoOpaqueNV
#define RayFlags_TerminateOnFirstHit gl_RayFlagsTerminateOnFirstHitNV
#define RayFlags_SkipClosestHitShader gl_RayFlagsSkipClosestHitShaderNV
#define RayFlags_CullBackFacingTriangles gl_RayFlagsCullBackFacingTrianglesNV
#define RayFlags_CullFrontFacingTriangles gl_RayFlagsCullFrontFacingTrianglesNV
#define RayFlags_CullOpaque gl_RayFlagsCullOpaqueNV
#define RayFlags_CullNoOpaque gl_RayFlagsCullNoOpaqueNV

// Common globals (prefixed with rt_)

#define rt_ObjectRayOrigin gl_ObjectRayOriginNV
#define rt_ObjectRayDirection gl_ObjectRayDirectionNV

#define rt_WorldRayOrigin gl_WorldRayOriginNV
#define rt_WorldRayDirection gl_WorldRayDirectionNV

#define rt_RayHitT gl_HitTNV

#define rt_WorldToObject gl_WorldToObjectNV
#define rt_ObjectToWorld gl_ObjectToWorldNV

#define rt_InstanceCustomIndex gl_InstanceCustomIndexNV

#define rt_LaunchID gl_LaunchIDNV
#define rt_LaunchSize gl_LaunchSizeNV

// Functions

#define ignoreIntersection() ignoreIntersectionNV()
#define traceRay(topLevelAS, rayFlags, cullMask, sbtRecordOffset, sbtRecordStride, missIndex, origin, tmin, direction, tmax, payloadIdx) \
    traceNV(topLevelAS, rayFlags, cullMask, sbtRecordOffset, sbtRecordStride, missIndex, origin, tmin, direction, tmax, payloadIdx)

#endif // RTX_RAY_TRACING_GLSL
