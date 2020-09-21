#version 460

#include <shared/CameraState.h>

layout(location = 0) in vec3 vNormal;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
}
