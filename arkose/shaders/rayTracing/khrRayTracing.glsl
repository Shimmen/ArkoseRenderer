#ifndef KHR_RAY_TRACING_GLSL
#define KHR_RAY_TRACING_GLSL

// https://github.com/KhronosGroup/GLSL/blob/master/extensions/ext/GLSL_EXT_ray_tracing.txt

#extension GL_EXT_ray_tracing : require

// Common types

#define AccelerationStructure accelerationStructureEXT

// Common type annotations

#define rayPayload rayPayloadEXT
#define rayPayloadIn rayPayloadInEXT
#define hitAttribute hitAttributeEXT

// Ray flags

#define RayFlags_None gl_RayFlagsNoneEXT
#define RayFlags_Opaque gl_RayFlagsOpaqueEXT
#define RayFlags_NoOpaque gl_RayFlagsNoOpaqueEXT
#define RayFlags_TerminateOnFirstHit gl_RayFlagsTerminateOnFirstHitEXT
#define RayFlags_SkipClosestHitShader gl_RayFlagsSkipClosestHitShaderEXT
#define RayFlags_CullBackFacingTriangles gl_RayFlagsCullBackFacingTrianglesEXT
#define RayFlags_CullFrontFacingTriangles gl_RayFlagsCullFrontFacingTrianglesEXT
#define RayFlags_CullOpaque gl_RayFlagsCullOpaqueEXT
#define RayFlags_CullNoOpaque gl_RayFlagsCullNoOpaqueEXT

// Common globals (prefixed with rt_)

#define rt_ObjectRayOrigin gl_ObjectRayOriginEXT
#define rt_ObjectRayDirection gl_ObjectRayDirectionEXT

#define rt_WorldRayOrigin gl_WorldRayOriginEXT
#define rt_WorldRayDirection gl_WorldRayDirectionEXT

#define rt_RayHitT gl_HitTEXT

#define rt_WorldToObject gl_WorldToObjectEXT
#define rt_ObjectToWorld gl_ObjectToWorldEXT

#define rt_InstanceCustomIndex gl_InstanceCustomIndexEXT

#define rt_LaunchID gl_LaunchIDEXT
#define rt_LaunchSize gl_LaunchSizeEXT

// NOTE: Not available in GL_NV_ray_tracing!
#define rt_HitKind gl_HitKindEXT
#define rt_HitKindFrontFace gl_HitKindFrontFacingTriangleEXT
#define rt_HitKindBackFace gl_HitKindBackFacingTriangleEXT

// Functions

#define ignoreIntersection() ignoreIntersectionEXT
#define traceRay(topLevelAS, rayFlags, cullMask, sbtRecordOffset, sbtRecordStride, missIndex, origin, tmin, direction, tmax, payloadIdx) \
    traceRayEXT(topLevelAS, rayFlags, cullMask, sbtRecordOffset, sbtRecordStride, missIndex, origin, tmin, direction, tmax, payloadIdx)

#endif // KHR_RAY_TRACING_GLSL
