#version 460

#include <shared/CameraState.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) out vec3 vNormal;
layout(location = 1) out flat int vProbeIdx;

layout(push_constant) uniform PushConstants {
    float probeScale;
    vec4 probeLocation;
    int probeIdx;
};

void main()
{
    vNormal = normalize(aPosition);
    vProbeIdx = probeIdx;

    vec4 worldSpacePos = vec4(probeScale * aPosition + probeLocation.xyz, 1.0);
    gl_Position = camera.projectionFromView * camera.viewFromWorld * worldSpacePos;
}
