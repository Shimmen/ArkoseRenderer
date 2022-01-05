#version 460

#include <shared/CameraState.h>

layout(location = 0) in vec2 aPosition;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) out vec4 vPosition;
layout(location = 1) out vec3 vViewRay;

void main()
{
    // Point on the near plane (we're not testing depth anyway, only stencil)
    vPosition = vec4(aPosition, 0.0, 1.0);

    vec4 viewSpaceRay = camera.viewFromProjection * vPosition;
    vViewRay = mat3(camera.worldFromView) * (viewSpaceRay.xyz / viewSpaceRay.w);

    gl_Position = vPosition;
}
