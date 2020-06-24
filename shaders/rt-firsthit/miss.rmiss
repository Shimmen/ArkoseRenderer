#version 460
#extension GL_NV_ray_tracing : require

#include <common/spherical.glsl>
#include <effects/water.glsl>

layout(location = 0) rayPayloadInNV vec3 hitValue;

layout(binding = 0, set = 2) uniform sampler2D environmentMap;
layout(binding = 3, set = 0) uniform TimeBlock { float time; };

void main()
{
#if 1
    vec2 sampleUv = sphericalUvFromDirection(gl_WorldRayDirectionNV);
    vec3 skyColor = texture(environmentMap, sampleUv).rgb;
    hitValue = skyColor;
#else
    hitValue = water(gl_WorldRayDirectionNV, 4.0, time).rgb;
#endif
}
