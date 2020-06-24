#version 460
#extension GL_NV_ray_tracing : require

#include <common/spherical.glsl>

layout(location = 0) rayPayloadInNV vec3 hitValue;

layout(binding = 6, set = 0) uniform EnvBlock { float envMultiplier; };
layout(binding = 7, set = 0) uniform sampler2D environmentMap;

void main()
{
    vec2 sampleUv = sphericalUvFromDirection(gl_WorldRayDirectionNV);
    vec3 skyColor = texture(environmentMap, sampleUv).rgb;
    hitValue = envMultiplier * skyColor;
}
