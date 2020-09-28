#version 460

#include <common/octahedral.glsl>
#include <common/spherical.glsl>
#include <shared/CameraState.h>

layout(location = 0) in vec3 vNormal;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };
layout(set = 1, binding = 0) uniform sampler2D probeDataTex;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 N = normalize(vNormal);

    vec3 probeSampleDir = vec3(N.x, -N.y, N.z);
    vec2 probeSampleUV = sphericalUvFromDirection(probeSampleDir);
    //vec2 probeSampleUV = octahedralEncode(probeSampleDir) * 0.5 + 0.5;

#if 0
    vec3 irradiance = texture(probeDataTex, probeSampleUV).rgb;
    oColor = vec4(irradiance, 1.0);
#else
    vec2 distances = texture(probeDataTex, probeSampleUV).rg;
    oColor = vec4(100.0 * vec3(distances.x), 1.0);
#endif
}
