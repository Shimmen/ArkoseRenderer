#ifndef HLSL_ON_GLSL_GLSL
#define HLSL_ON_GLSL_GLSL

// References:
// https://anteru.net/blog/2016/mapping-between-HLSL-and-GLSL/
// https://learn.microsoft.com/en-us/windows/uwp/gaming/glsl-to-hlsl-reference

#define int2 ivec2
#define int3 ivec3
#define int4 ivec4

#define uint2 uvec2
#define uint3 uvec3
#define uint4 uvec4

#define float2 vec2
#define float3 vec3
#define float4 vec4

// TODO: See if we can be more precise for these types
#define min16float float
#define min16float2 vec2
#define min16float3 vec3
#define min16float4 vec4

#define frac fract
#define lerp mix

#define groupshared shared

#define GroupMemoryBarrierWithGroupSync() groupMemoryBarrier(); barrier();

#endif // HLSL_ON_GLSL_GLSL
