#version 460

#include <common/camera.glsl>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = camera.projectionFromView * camera.viewFromWorld * vec4(aPosition, 1.0);
}
