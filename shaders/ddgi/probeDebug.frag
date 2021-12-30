#version 460

#include <common/namedUniforms.glsl>
#include <ddgi/probeSampling.glsl>

layout(location = 0) in vec3 vNormal;

layout(set = 1, binding = 0) uniform DDGIGridDataBlock { DDGIProbeGridData ddgiProbeGridData; };
layout(set = 1, binding = 1) uniform sampler2D ddgiIrradianceAtlas;
layout(set = 1, binding = 2) uniform sampler2D ddgiVisibilityAtlas;

NAMED_UNIFORMS(pushConstants,
    int debugVisualisation;
    ivec3 probeGridCoord;
    vec3 probeLocation;
    float probeScale;
    float distanceScale;
)

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 probeSampleDir = normalize(vNormal);
    vec3 irradiance = sampleIrradianceProbe(ddgiProbeGridData, pushConstants.probeGridCoord, probeSampleDir, ddgiIrradianceAtlas);
    vec2 visibility = sampleVisibilityProbe(ddgiProbeGridData, pushConstants.probeGridCoord, probeSampleDir, ddgiVisibilityAtlas);

    switch (pushConstants.debugVisualisation) {
    case DDGI_PROBE_DEBUG_VISUALIZE_IRRADIANCE:
        outColor = vec4(irradiance, 1.0);
        return;
    case DDGI_PROBE_DEBUG_VISUALIZE_DISTANCE:
        outColor = vec4(vec3(pushConstants.distanceScale * visibility.x), 1.0);
        return;
    case DDGI_PROBE_DEBUG_VISUALIZE_DISTANCE2:
        outColor = vec4(vec3(pushConstants.distanceScale * visibility.y), 1.0);
        return;
    }

    outColor = vec4(1, 0, 1, 1);
}
