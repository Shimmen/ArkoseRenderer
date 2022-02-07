#version 460

#include <common/rayTracing.glsl>
#include <common/spherical.glsl>

layout(location = 0) rayPayloadIn vec3 hitValue;

layout(set = 3, binding = 0) uniform sampler2D environmentMap;

void main()
{
    vec2 sampleUv = sphericalUvFromDirection(rt_WorldRayDirection);
    vec3 skyColor = texture(environmentMap, sampleUv).rgb;
    hitValue = skyColor;
}
