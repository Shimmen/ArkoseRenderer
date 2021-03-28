#version 460

#include <common/namedUniforms.glsl>
#include <shared/CameraState.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) out vec3 vNormal;
layout(location = 1) out flat int vProbeIdx;

NAMED_UNIFORMS(pushConstants,
    int probeIdx;
    vec3 probeLocation;
    float probeScale;
    float indirectExposure;
)

void main()
{
    vNormal = normalize(aPosition);
    vProbeIdx = pushConstants.probeIdx;

    vec4 worldSpacePos = vec4(pushConstants.probeScale * aPosition + pushConstants.probeLocation.xyz, 1.0);
    gl_Position = camera.projectionFromView * camera.viewFromWorld * worldSpacePos;
}
