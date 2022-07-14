#version 460

#include <common/namedUniforms.glsl>
#include <ddgi/probeSampling.glsl>
#include <shared/CameraState.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };
layout(set = 1, binding = 0) uniform DDGIGridDataBlock { DDGIProbeGridData ddgiProbeGridData; };

layout(location = 0) out vec3 vNormal;
layout(location = 1) flat out uint vProbeIdx;

NAMED_UNIFORMS(pushConstants,
    int debugVisualisation;
    float probeScale;
    float distanceScale;
)

void main()
{
    vNormal = normalize(aPosition);

    vProbeIdx = uint(gl_InstanceIndex);
    vec3 probePosition = calculateProbePosition(ddgiProbeGridData, vProbeIdx);

    vec4 worldSpacePos = vec4(pushConstants.probeScale * aPosition + probePosition, 1.0);
    gl_Position = camera.projectionFromView * camera.viewFromWorld * worldSpacePos;
}
