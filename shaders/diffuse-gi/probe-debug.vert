#version 460

#include <shared/CameraState.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) out vec3 vNormal;

layout(push_constant) uniform PushConstants {
    vec4 probeLocation;
    float probeScale;
};

void main()
{
    vNormal = normalize(aPosition);
    vec4 worldSpacePos = vec4(probeScale * aPosition + probeLocation.xyz, 1.0);
    gl_Position = camera.projectionFromView * camera.viewFromWorld * worldSpacePos;
}
