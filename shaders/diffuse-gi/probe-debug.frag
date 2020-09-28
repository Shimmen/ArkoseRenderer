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
    vec3 irradiance = texture(probeDataTex, probeSampleUV).rgb;

    //oColor = vec4(N * 0.5 + 0.5, 1.0);
    oColor = vec4(irradiance, 1.0);
}
