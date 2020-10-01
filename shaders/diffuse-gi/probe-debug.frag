#version 460

#include <common/octahedral.glsl>
#include <common/spherical.glsl>
#include <shared/CameraState.h>
#include <shared/ProbeDebug.h>

layout(location = 0) in vec3 vNormal;
layout(location = 1) in flat int vProbeIdx;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };
layout(set = 1, binding = 0) uniform sampler2DArray probeDataTex;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 N = normalize(vNormal);

    vec3 probeSampleDir = vec3(N.x, -N.y, N.z);
    vec2 probeSampleUV = sphericalUvFromDirection(probeSampleDir);
    //vec2 probeSampleUV = octahedralEncode(probeSampleDir) * 0.5 + 0.5;

    vec3 uvWithArrayIdx = vec3(probeSampleUV, float(vProbeIdx));

#if PROBE_DEBUG_VIZ == PROBE_DEBUG_VISUALIZE_COLOR
    vec3 irradiance = texture(probeDataTex, uvWithArrayIdx).rgb;
    oColor = vec4(irradiance, 1.0);
#elif PROBE_DEBUG_VIZ == PROBE_DEBUG_VISUALIZE_DISTANCE
    vec2 distances = texture(probeDataTex, uvWithArrayIdx).rg;
    oColor = vec4(100.0 * vec3(distances.x), 1.0);
#elif PROBE_DEBUG_VIZ == PROBE_DEBUG_VISUALIZE_DISTANCE2
    vec2 distances = texture(probeDataTex, uvWithArrayIdx).rg;
    oColor = vec4(100.0 * vec3(distances.y), 1.0);
#else
    oColor = 9999.9 * vec4(1, 0, 1, 1);
#endif
}
